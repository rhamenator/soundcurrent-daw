// SPDX-License-Identifier: GPL-3.0-only
#include "wave_check_controller.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QWaitCondition>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
namespace soundcurrent::daw::ui {
namespace {
void check(bool ok,const char *message,ErrorCode code=ErrorCode::InvalidState) {if (!ok) throw ProjectError(code,message);}
void canceled(std::stop_token stop) {check(!stop.stop_requested(),"Media check canceled",ErrorCode::Canceled);}
QString qpath(const std::filesystem::path &path) {const auto bytes=path.u8string();return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),static_cast<qsizetype>(bytes.size()));}
}
struct WaveCheckController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    WaveCheckOptions options;
    std::shared_ptr<const WaveSelection> queued;
    std::stop_source stop;
    std::uint64_t sequence=0;
    bool closing=false,busy=false;
    WaveCheckSnapshot view;
    std::shared_ptr<const WaveCheckSnapshot> latest=std::make_shared<const WaveCheckSnapshot>();
    explicit State(WaveCheckOptions value):options(std::move(value)) {
        if (options.program.isEmpty()) options.program=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-approved-wave-probe.exe");
#else
            QStringLiteral("/sc-approved-wave-probe");
#endif
    }
    void publish() {auto copy=std::make_shared<const WaveCheckSnapshot>(view);QMutexLocker lock(&mutex);latest=std::move(copy);}
    void execute(std::stop_token token) {
        canceled(token);
        check(QFileInfo(options.program).isAbsolute() && options.maximumSourceBytes && options.deadlineMilliseconds,"Invalid trusted media-check policy");
        constexpr std::size_t childCredit=16*1024*1024,protocolCredit=waveReportMaximumBytes*2;
        auto loan=options.memory.reserve(childCredit+32768+protocolCredit+waveReportParserCharge+waveReportValueCharge);
        std::string output(waveReportMaximumBytes,'\0');std::array<char,1024> errors{};std::size_t used=0,errorUsed=0;
        if (options.beforeSpawn) options.beforeSpawn();
        canceled(token);
        QProcess child;child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&]{view.childPid=static_cast<std::size_t>(child.processId());publish();});
        const QStringList arguments{view.selection->selectedFilename ? QStringLiteral("--desktop-file") : QStringLiteral("--desktop-check"),QStringLiteral("--root"),qpath(view.selection->root),
            QStringLiteral("--relative"),QString::fromUtf8(view.selection->relative.data(),static_cast<qsizetype>(view.selection->relative.size())),
            QStringLiteral("--maximum-bytes"),QString::number(view.selection->maximumBytes)};
        child.start(options.program,arguments,QIODevice::ReadOnly);view.phase=WaveCheckPhase::Checking;publish();
        const auto begun=std::chrono::steady_clock::now();auto stoppingAt=begun;
        bool stopping=false,killed=false,overflow=false;
        auto drain=[&](QProcess::ProcessChannel channel,char *bank,std::size_t capacity,std::size_t &filled) {
            child.setReadChannel(channel);
            while (child.bytesAvailable()>0) {
                if (filled==capacity) {overflow=true;return;}
                const auto n=child.read(bank+filled,static_cast<qint64>(capacity-filled));
                check(n>=0,"Media check channel failed",ErrorCode::Io);if (!n) break;filled+=static_cast<std::size_t>(n);
            }
        };
        try {
            while (child.state()!=QProcess::NotRunning) {
                child.waitForReadyRead(20);
                if (!view.childPid && child.processId()>0) {view.childPid=static_cast<std::size_t>(child.processId());publish();}
                drain(QProcess::StandardOutput,output.data(),output.size(),used);
                drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
                const auto now=std::chrono::steady_clock::now();const bool timeout=now-begun>=std::chrono::milliseconds(options.deadlineMilliseconds);
                if (!stopping && (token.stop_requested() || timeout || overflow)) {
                    stopping=true;stoppingAt=now;view.timedOut=timeout;child.terminate();publish();
                    if (overflow) {child.kill();killed=true;}
                }
                if (stopping && !killed && now-stoppingAt>=std::chrono::milliseconds(200)) {child.kill();killed=true;}
                if (stopping) child.waitForFinished(20);
            }
            drain(QProcess::StandardOutput,output.data(),output.size(),used);
            drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
        } catch (...) {
            child.kill();while (child.state()!=QProcess::NotRunning) child.waitForFinished(20);
            if (view.childPid) view.childExit=child.exitCode();
            throw;
        }
        check(view.childPid,"Media checker failed to start",ErrorCode::Io);view.childExit=child.exitCode();
        loan.resize(loan.bytes()-childCredit); // Only after this exact owner is terminal.
        canceled(token);check(!overflow,"Media checker exceeded output limits",ErrorCode::ResourceLimit);
        check(!view.timedOut,"Media checker exceeded deadline",ErrorCode::Io);
        if (child.exitStatus()!=QProcess::NormalExit || *view.childExit!=0) {
            // Exact bounded known diagnostics only; no raw foreign text reaches UI.
            for (const auto code:{ErrorCode::InvalidState,ErrorCode::UnsupportedSchema,ErrorCode::InvalidParameter,
                ErrorCode::Io,ErrorCode::MissingMedia,ErrorCode::MediaMismatch,ErrorCode::Canceled,ErrorCode::ResourceLimit}) {
                const auto expected=nlohmann::json({{"protocol","sc-approved-wave-validation-v2"},{"complete",false},
                    {"messageId","import.wave_validation_failed"},{"errorCode",static_cast<unsigned>(code)}}).dump();
                const auto received=std::string_view(errors.data(),errorUsed);
                if (received==expected+"\n" || received==expected+"\r\n")
                    throw ProjectError(code,"Approved media checker refused source");
            }
            throw ProjectError(ErrorCode::Io,"Media checker refused or crashed");
        }
        check(!errorUsed,"Successful media check included an error channel");output.resize(used);
        if (options.afterChild) options.afterChild(output,view.childPid);
        check(output.size()<=waveReportMaximumBytes,"Media report hook exceeds admitted bank");
        ResourceLease values,parser,encoded;loan.transferTo(values,waveReportValueCharge);loan.transferTo(parser,waveReportParserCharge);loan.transferTo(encoded,protocolCredit);
        auto result=decodeWaveCheckReport(OwnedInspectionProtocol(std::move(encoded),std::move(output)),view.selection->relative,
            view.childPid,view.selection->maximumBytes,options.memory,std::move(values),std::move(parser),token);
        view.report=std::make_shared<const WaveCheckReport>(std::move(result));view.phase=WaveCheckPhase::Complete;
    }
    void run() override {
        for (;;) {
            std::stop_token token;
            {QMutexLocker lock(&mutex);while (!queued && !closing) wake.wait(&mutex);if (!queued) break;
             view=*latest;queued.reset();token=stop.get_token();}
            try {execute(token);}
            catch (const ProjectError &e) {view.error=e.code();view.phase=e.code()==ErrorCode::Canceled ? WaveCheckPhase::Canceled : WaveCheckPhase::Fault;}
            catch (const std::exception &) {view.error=ErrorCode::Io;view.phase=WaveCheckPhase::Fault;}
            view.busy=false;
            {QMutexLocker lock(&mutex);busy=false;latest=std::make_shared<const WaveCheckSnapshot>(view);if (closing) break;}
        }
        view.selection.reset();view.report.reset();view.busy=false;view.closed=true;view.phase=WaveCheckPhase::Closed;publish();
    }
};
WaveCheckController::WaveCheckController(WaveCheckOptions options):state_(std::make_unique<State>(std::move(options))) {state_->start(QThread::LowPriority);}
WaveCheckController::~WaveCheckController() {requestShutdown();state_->wait();}
Admission WaveCheckController::submit(std::filesystem::path root,std::string relative,std::uint64_t maximum) {
    return enqueue(std::move(root),std::move(relative),maximum,false);
}
Admission WaveCheckController::submitReplacement(std::filesystem::path file,std::uint64_t maximum) {
    check(file.is_absolute(),"Replacement must be an explicitly selected absolute file",ErrorCode::InvalidParameter);
    const auto name=file.filename().u8string();
    return enqueue(file.parent_path(),std::string(reinterpret_cast<const char *>(name.data()),name.size()),maximum,true);
}
Admission WaveCheckController::enqueue(std::filesystem::path root,std::string relative,std::uint64_t maximum,bool selected) {
    QMutexLocker lock(&state_->mutex);if (state_->closing) return Admission::Closing;
    if (state_->busy || state_->sequence==std::numeric_limits<std::uint64_t>::max()) return Admission::Full;
    check(root.is_absolute() && root.native().size()<=4096 && relative.size()<=1024 && !relative.empty() && validUtf8(relative),"Invalid explicit media selection",ErrorCode::InvalidParameter);
    if (!maximum) maximum=state_->options.maximumSourceBytes;
    check(maximum>0,"Invalid media byte limit",ErrorCode::InvalidParameter);
    auto selection=std::make_shared<const WaveSelection>(state_->options.memory.reserve(16384),std::move(root),std::move(relative),maximum,selected);
    WaveCheckSnapshot next;next.phase=WaveCheckPhase::Queued;next.job=state_->sequence+1;next.selection=selection;next.busy=true;
    auto published=std::make_shared<const WaveCheckSnapshot>(std::move(next));std::stop_source preparedStop;
    state_->sequence=published->job;state_->stop=std::move(preparedStop);state_->queued=std::move(selection);
    state_->busy=true;state_->latest=std::move(published);state_->wake.wakeOne();return Admission::Accepted;
}
void WaveCheckController::requestCancel() noexcept {QMutexLocker lock(&state_->mutex);if (state_->busy) state_->stop.request_stop();state_->wake.wakeOne();}
void WaveCheckController::requestShutdown() noexcept {QMutexLocker lock(&state_->mutex);state_->closing=true;state_->stop.request_stop();state_->wake.wakeOne();}
bool WaveCheckController::clearResult() {
    QMutexLocker lock(&state_->mutex);if (state_->busy || state_->closing) return false;
    auto empty=std::make_shared<const WaveCheckSnapshot>();state_->view=WaveCheckSnapshot{};state_->latest=std::move(empty);return true;
}
std::shared_ptr<const WaveCheckSnapshot> WaveCheckController::snapshot() const {QMutexLocker lock(&state_->mutex);return state_->latest;}
} // namespace soundcurrent::daw::ui
