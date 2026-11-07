// SPDX-License-Identifier: GPL-3.0-only
#include "waveform_controller.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QWaitCondition>
#include <chrono>

namespace soundcurrent::daw::ui {
namespace {
void check(bool ok, const char *message, ErrorCode code = ErrorCode::InvalidState) {
    if (!ok) throw ProjectError(code, message);
}
void canceled(std::stop_token stop) {
    check(!stop.stop_requested(), "Waveform request canceled", ErrorCode::Canceled);
}
QString pathString(const std::filesystem::path &p) {
    const auto bytes = p.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}
} // namespace
struct WaveformController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    WaveformControllerOptions options;
    std::shared_ptr<const OwnedWaveformSelection> queued;
    std::stop_source stop;
    bool busy = false, closing = false, retire = false;
    WaveformSnapshot view;
    std::shared_ptr<const WaveformSnapshot> latest = std::make_shared<const WaveformSnapshot>();
    explicit State(WaveformControllerOptions o) : options(std::move(o)) {
        check(bool(options.current) && options.maximumSourceBytes &&
            options.maximumSourceBytes <= 8ULL * 1024 * 1024 * 1024 && options.deadlineMilliseconds &&
            options.childMemoryBytes >= 1024 * 1024 && options.childMemoryBytes <= 256ULL * 1024 * 1024,
            "Invalid trusted waveform controller policy", ErrorCode::InvalidParameter);
        if (options.program.isEmpty()) options.program = QCoreApplication::applicationDirPath() +
#ifdef _WIN32
            QStringLiteral("/sc-waveform-worker.exe");
#else
            QStringLiteral("/sc-waveform-worker");
#endif
        check(QFileInfo(options.program).isAbsolute(), "Waveform helper must be absolute", ErrorCode::InvalidParameter);
    }
    void publish() {
        auto copy = std::make_shared<const WaveformSnapshot>(view);
        QMutexLocker lock(&mutex); latest = std::move(copy);
    }
    bool current() {
        const auto now = options.current();
        return now && view.selection && *now == view.selection->value;
    }
    void fresh() {
        if (!current()) { view.phase = WaveformPhase::Stale; throw ProjectError(ErrorCode::InvalidState, "Waveform selection replaced"); }
    }
    void execute(std::stop_token token) {
        using Clock = std::chrono::steady_clock;
        const auto begun = view.selection->admittedAt;
        const auto deadline = [&] { return Clock::now() - begun >= std::chrono::milliseconds(options.deadlineMilliseconds); };
        const auto checkDeadline = [&] {
            if (deadline()) { view.timedOut = true; throw ProjectError(ErrorCode::Io, "Waveform deadline exceeded"); }
        };
        canceled(token); fresh();
        const auto &selected = view.selection->value; const auto &t = selected.ticket;
        WaveformLimits limits; limits.baseFramesPerBin = t.baseFramesPerBin; limits.window = t.window;
        const auto capacity = waveformReplySize(t, limits);
        // This same ledger must admit the decoded model while receive storage
        // still exists. Reserve the child allowance before any process starts.
        auto childCredit = options.memory.reserve(options.childMemoryBytes);
        auto channelCredit = options.memory.reserve(capacity + 65536);
        auto output = std::make_unique<char[]>(capacity);
        std::array<char, 1024> errors{};
        std::size_t used = 0, errorUsed = 0;
        if (options.beforeSpawn) options.beforeSpawn();
        canceled(token); fresh(); checkDeadline();
        const QStringList args{
            "--root", pathString(selected.root), "--relative", QString::fromUtf8(selected.relative),
            "--sha256", QString::fromLatin1(t.source.sha256.data(), 64),
            "--frames", QString::number(t.source.frames), "--rate", QString::number(t.source.rate),
            "--channels", QString::number(t.source.channels), "--base-frames", QString::number(t.baseFramesPerBin),
            "--operation", QString::fromStdString(t.operation.str()), "--epoch", QString::number(t.projectEpoch),
            "--generation", QString::number(t.generation), "--maximum-bytes", QString::number(options.maximumSourceBytes),
            "--memory-bytes", QString::number(qulonglong(options.childMemoryBytes)),
            "--window-first", QString::number(t.window.first), "--window-frames", QString::number(t.window.frames)};
        QProcess child; child.setProcessChannelMode(QProcess::SeparateChannels);
        bool stopping = false, killed = false, overflow = false, stale = false;
        auto stoppingAt = begun;
        auto observePid = [&] {
            if (!view.childPid && child.processId() > 0) { view.childPid = std::uint64_t(child.processId()); publish(); }
        };
        QObject::connect(&child, &QProcess::started, &child, observePid);
        const auto drain = [&](QProcess::ProcessChannel channel, char *bank, std::size_t maximum, std::size_t &filled) {
            child.setReadChannel(channel);
            while (child.bytesAvailable() > 0) {
                if (filled == maximum) { overflow = true; return; }
                const auto n = child.read(bank + filled, qint64(maximum - filled));
                check(n >= 0, "Waveform channel read failed", ErrorCode::Io);
                if (!n) break;
                filled += std::size_t(n);
            }
        };
        const auto drainBoth = [&] {
            drain(QProcess::StandardOutput, output.get(), capacity, used);
            drain(QProcess::StandardError, errors.data(), errors.size(), errorUsed);
        };
        try {
            child.start(options.program, args, QIODevice::ReadOnly);
            view.phase = WaveformPhase::Running; publish();
            while (child.state() != QProcess::NotRunning) {
                child.waitForReadyRead(20); observePid(); drainBoth();
                if (!stopping) {
                    stale = !current(); view.timedOut = deadline();
                    if (token.stop_requested() || stale || view.timedOut || overflow) {
                        stopping = true; stoppingAt = Clock::now(); child.terminate();
                        if (overflow) { child.kill(); killed = true; }
                    }
                }
                if (stopping && !killed && Clock::now() - stoppingAt >= std::chrono::milliseconds(200)) {
                    child.kill(); killed = true;
                }
                if (stopping) child.waitForFinished(20);
            }
            drainBoth();
        } catch (...) {
            child.kill(); while (child.state() != QProcess::NotRunning) child.waitForFinished(20);
            if (view.childPid) view.childExit = child.exitCode();
            throw;
        }
        if (view.childPid) view.childExit = child.exitCode();
        childCredit.resize(0); // Exact child is terminal, including a failed start.
        canceled(token);
        if (stale) { view.phase = WaveformPhase::Stale; throw ProjectError(ErrorCode::InvalidState, "Waveform owner changed during work"); }
        check(!overflow, "Waveform output exceeded receive bank", ErrorCode::ResourceLimit);
        check(!view.timedOut, "Waveform child exceeded deadline", ErrorCode::Io); checkDeadline(); fresh();
        check(view.childPid && view.childExit, "Waveform child failed to start", ErrorCode::Io);
        if (child.exitStatus() != QProcess::NormalExit || *view.childExit != 0) {
            const auto diagnostic = std::string_view(errors.data(), errorUsed);
            for (const auto code : {ErrorCode::InvalidId, ErrorCode::InvalidState, ErrorCode::UnsupportedSchema,
                    ErrorCode::InvalidParameter, ErrorCode::Io, ErrorCode::MissingMedia,
                    ErrorCode::MediaMismatch, ErrorCode::Canceled, ErrorCode::ResourceLimit}) {
                const auto known = "sc-waveform-v2:error:" + std::to_string(static_cast<unsigned>(code));
                if (!used && (diagnostic == known + "\n" || diagnostic == known + "\r\n"))
                    throw ProjectError(code, "Waveform worker refused source");
            }
            throw ProjectError(ErrorCode::Io, "Waveform worker failed");
        }
        check(!errorUsed && used == capacity, "Incomplete or contradictory waveform reply");
        if (options.afterChild) options.afterChild({output.get(), used}, view.childPid);
        canceled(token); checkDeadline(); fresh();
        auto wave = decodeWaveformReply({output.get(), used}, t, view.childPid, options.memory, limits, token);
        canceled(token); checkDeadline(); fresh();
        view.waveform = std::make_shared<const Waveform>(std::move(wave)); view.phase = WaveformPhase::Complete;
    }
    void run() override {
        for (;;) {
            std::stop_token token;
            { QMutexLocker lock(&mutex);
              while (!queued && !closing && !retire) wake.wait(&mutex);
              if (retire) { view = *latest; retire = false; }
              if (!queued) { if (closing) break; continue; }
              view = *latest; queued.reset(); token = stop.get_token(); }
            try { execute(token); }
            catch (const ProjectError &e) {
                view.waveform.reset(); view.error = e.code();
                if (e.code() == ErrorCode::Canceled) view.phase = WaveformPhase::Canceled;
                else if (view.phase != WaveformPhase::Stale) view.phase = WaveformPhase::Fault;
            } catch (...) { view.waveform.reset(); view.error = ErrorCode::Io; view.phase = WaveformPhase::Fault; }
            { QMutexLocker lock(&mutex);
              // Cancellation can arrive after the last decode poll but before
              // terminal publication; serialize that boundary with requests.
              if (token.stop_requested()) { view.waveform.reset(); view.error = ErrorCode::Canceled; view.phase = WaveformPhase::Canceled; }
              view.busy = false; busy = false; latest = std::make_shared<const WaveformSnapshot>(view); }
        }
        view.selection.reset(); view.waveform.reset(); view.busy = false;
        view.closed = true; view.phase = WaveformPhase::Closed; publish();
    }
};
WaveformController::WaveformController(WaveformControllerOptions o) : state_(std::make_unique<State>(std::move(o))) { state_->start(QThread::LowPriority); }
WaveformController::~WaveformController() { requestShutdown(); state_->wait(); }
Admission WaveformController::submit(WaveformSelection value) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing) return Admission::Closing;
    if (state_->busy) return Admission::Full;
    check(value.root.is_absolute() && value.root.native().size() <= 4096 && !value.relative.empty() &&
        value.relative.size() <= 1024 && validUtf8(value.relative) && value.relative.find('\0') == std::string::npos,
        "Invalid waveform selection path", ErrorCode::InvalidParameter);
    const auto rootBytes = value.root.u8string();
    const std::string_view rootUtf8(reinterpret_cast<const char *>(rootBytes.data()), rootBytes.size());
    check(rootUtf8.size() <= 4096 && validUtf8(rootUtf8) && rootUtf8.find('\0') == rootUtf8.npos,
          "Invalid waveform root encoding", ErrorCode::InvalidParameter);
    WaveformLimits limits; limits.baseFramesPerBin = value.ticket.baseFramesPerBin; limits.window = value.ticket.window;
    (void)waveformReplySize(value.ticket, limits);
    auto selection = std::make_shared<const OwnedWaveformSelection>(OwnedWaveformSelection{
        state_->options.memory.reserve(32768), std::move(value), std::chrono::steady_clock::now()});
    WaveformSnapshot next; next.phase = WaveformPhase::Queued; next.busy = true; next.selection = selection;
    auto published = std::make_shared<const WaveformSnapshot>(std::move(next)); std::stop_source token;
    state_->stop = std::move(token); state_->queued = std::move(selection); state_->latest = std::move(published);
    state_->busy = true; state_->wake.wakeOne(); return Admission::Accepted;
}
void WaveformController::requestCancel() noexcept { QMutexLocker lock(&state_->mutex); if (state_->busy) state_->stop.request_stop(); state_->wake.wakeOne(); }
void WaveformController::requestShutdown() noexcept { QMutexLocker lock(&state_->mutex); state_->closing = true; state_->stop.request_stop(); state_->wake.wakeOne(); }
bool WaveformController::clearResult() {
    QMutexLocker lock(&state_->mutex); if (state_->busy || state_->closing) return false;
    state_->latest = std::make_shared<const WaveformSnapshot>(); state_->retire = true; state_->wake.wakeOne(); return true;
}
std::shared_ptr<const WaveformSnapshot> WaveformController::snapshot() const { QMutexLocker lock(&state_->mutex); return state_->latest; }
} // namespace soundcurrent::daw::ui
