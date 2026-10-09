// SPDX-License-Identifier: GPL-3.0-only
#include "import_inspection_controller.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>
#include <chrono>
namespace soundcurrent::daw::ui {
namespace {
QString qpath(const std::filesystem::path &path) {
    const auto bytes=path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),static_cast<qsizetype>(bytes.size()));
}
void check(bool condition,const char *message,ErrorCode code=ErrorCode::InvalidState) {
    if (!condition) throw ProjectError(code,message);
}
void canceled(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Inspection canceled");
}
std::string id(ErrorCode code) {
    if (code==ErrorCode::Canceled) return "import.canceled";
    if (code==ErrorCode::ResourceLimit) return "import.resource_limit";
    if (code==ErrorCode::Io) return "import.io_error";
    return "import.protocol_error";
}
}
struct ImportInspectionController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    InspectionOptions options;
    struct Request {
        InspectionOperation operation;
        std::filesystem::path path;
        std::shared_ptr<const ImportInspectionReport> report;
    };
    std::optional<Request> queued;
    std::stop_source activeStop;
    bool closing=false, busy=false;
    std::uint64_t sequence=0;
    InspectionSnapshot view;
    std::shared_ptr<const InspectionSnapshot> latest=std::make_shared<const InspectionSnapshot>();
    explicit State(InspectionOptions value) : options(std::move(value)) {
        if (options.program.isEmpty()) {
#ifdef _WIN32
            const auto name=QStringLiteral("/sc-import-inspect-worker.exe");
#else
            const auto name=QStringLiteral("/sc-import-inspect-worker");
#endif
            options.program=QCoreApplication::applicationDirPath()+name;
        }
    }
    void publish() {
        auto snapshot=std::make_shared<const InspectionSnapshot>(view);
        QMutexLocker lock(&mutex); latest=std::move(snapshot);
    }
    void execute(const std::filesystem::path &path,std::stop_token stop) {
        canceled(stop);
        check(QFileInfo(options.program).isAbsolute() && options.maximumInputBytes &&
              options.maximumReportBytes && options.childMemoryBytes && options.deadlineMilliseconds,
              "Invalid trusted inspection options");
        view.phase=InspectionPhase::Capturing; publish();
        auto source=readForeignSnapshot(path,options.maximumInputBytes,options.memory,stop);
        const auto lines=foreignSnapshotLines(source.bytes(),stop);
        check(lines && lines<=ReaperStructureLimits{}.maximumLines,"Input outside inspection line envelope");
        const auto hash=hashForeignSnapshot(source.bytes(),options.memory,stop);
        const auto rowsBytes=inspectionRowsCharge(lines,true);
        const auto usage=options.memory.usage();
        PayloadCharge fixed("Inspection fixed work",usage.limitBytes);
        fixed.add(rowsBytes); fixed.add(options.childMemoryBytes); fixed.add(32768+32);
        const auto available=usage.limitBytes-usage.reservedBytes;
        constexpr auto expansion=inspectionDecoderExpansion+2; // Decoder plus conservative string capacity.
        if (available<=fixed.bytes()+expansion*512)
            throw ResourceLimitError("Inspection work",fixed.bytes()+expansion*512,available);
        PayloadCharge encodedBound("Inspection report estimate",std::numeric_limits<std::size_t>::max());
        encodedBound.add(2048); encodedBound.add(lines,4096);
        const auto reportBytes=std::min({encodedBound.bytes(),options.maximumReportBytes,
                                        (available-fixed.bytes())/expansion});
        const auto parserBytes=inspectionParserCharge(reportBytes);
        const auto protocolBytes=inspectionProtocolCharge(reportBytes);
        PayloadCharge work("Inspection admitted work",usage.limitBytes);
        work.add(rowsBytes); work.add(options.childMemoryBytes); work.add(parserBytes); work.add(protocolBytes);
        auto loan=options.memory.reserve(work.bytes()); // Before any child or response bank.
        std::string output(reportBytes,'\0'); std::array<char,4096> errors{};
        std::size_t used=0, errorUsed=0;
        if (options.beforeSpawn) options.beforeSpawn();
        canceled(stop);
        QProcess child;
        child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&] {
            view.childPid=static_cast<std::size_t>(child.processId()); publish();
        });
        const QStringList arguments{QStringLiteral("--rpp-properties"),qpath(path),QStringLiteral("--memory-bytes"),
            QString::number(options.childMemoryBytes),QStringLiteral("--maximum-input-bytes"),
            QString::number(options.maximumInputBytes),QStringLiteral("--maximum-report-bytes"),
            QString::number(reportBytes)};
        child.start(options.program,arguments,QIODevice::ReadOnly);
        const auto begun=std::chrono::steady_clock::now();
        bool stopping=false, killed=false, overflow=false;
        auto stoppingAt=begun;
        auto drain=[&](QProcess::ProcessChannel channel,char *bank,std::size_t capacity,std::size_t &filled) {
            child.setReadChannel(channel);
            while (child.bytesAvailable()>0) {
                if (filled==capacity) { overflow=true; return; }
                const auto count=child.read(bank+filled,static_cast<qint64>(std::min<std::size_t>(65536,capacity-filled)));
                check(count>=0,"Inspection channel read failed",ErrorCode::Io);
                if (!count) break;
                filled+=static_cast<std::size_t>(count);
            }
        };
        view.phase=InspectionPhase::Inspecting; publish();
        try {
        while (child.state()!=QProcess::NotRunning) {
            child.waitForReadyRead(20); // Worker thread only, pumps both channels.
            if (!view.childPid && child.processId()>0) {
                view.childPid=static_cast<std::size_t>(child.processId()); publish();
            }
            drain(QProcess::StandardOutput,output.data(),output.size(),used);
            drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
            const auto now=std::chrono::steady_clock::now();
            const bool timeout=now-begun>=std::chrono::milliseconds(options.deadlineMilliseconds);
            if (!stopping && (stop.stop_requested() || timeout || overflow)) {
                view.canceled=stop.stop_requested(); view.timedOut=timeout;
                stopping=true; stoppingAt=now; child.terminate(); publish();
                if (overflow) { child.kill(); killed=true; }
            }
            if (stopping && !killed && now-stoppingAt>=std::chrono::milliseconds(200)) {
                child.kill(); killed=true;
            }
            if (stopping) child.waitForFinished(20);
        }
        // NotRunning is terminal (or a failed start), not an observation timeout.
        // Keep loan/banks and the exact QProcess owner until this point.
        drain(QProcess::StandardOutput,output.data(),output.size(),used);
        drain(QProcess::StandardError,errors.data(),errors.size(),errorUsed);
        } catch (...) {
            child.kill();
            while (child.state()!=QProcess::NotRunning) child.waitForFinished(20);
            if (view.childPid) view.childExit=child.exitCode();
            throw;
        }
        check(view.childPid!=0,"Inspection process failed to start",ErrorCode::Io);
        view.childExit=child.exitCode();
        loan.resize(loan.bytes()-options.childMemoryBytes); // Child payload retired.
        canceled(stop);
        check(!view.timedOut,"Inspection process deadline expired",ErrorCode::Io);
        check(!overflow,"Inspection process exceeded response bound",ErrorCode::ResourceLimit);
        if (child.exitStatus()!=QProcess::NormalExit || *view.childExit!=0) {
            view.messageId="import.worker_failure";
            if (errorUsed) {
                for (const std::string name : {"import.resource_limit","import.invalid_structure",
                     "import.io_error","import.canceled","import.invalid_request"}) {
                    const auto expected="{\"protocol\":\"sc-import-inspection-v1\",\"complete\":false,\"messageId\":\""+
                        name+"\"}\n";
                    if (std::string_view(errors.data(),errorUsed)==expected) view.messageId=name;
                }
            }
            throw ProjectError(ErrorCode::Io,"Inspection child refused or crashed");
        }
        check(!errorUsed,"Successful inspection included an error channel");
        output.resize(used);
        if (options.afterChild) options.afterChild(output,view.childPid);
        check(output.size()<=reportBytes,"Qualification hook exceeded admitted bank");
        view.phase=InspectionPhase::Decoding; publish();
        ResourceLease rowGrant,parserGrant,encodedGrant;
        loan.transferTo(rowGrant,rowsBytes); loan.transferTo(parserGrant,parserBytes);
        loan.transferTo(encodedGrant,protocolBytes);
        auto result=decodeInspectionReport(OwnedInspectionProtocol(std::move(encodedGrant),std::move(output)),
            std::move(source),hash,view.childPid,
            options.memory,std::move(rowGrant),std::move(parserGrant),stop);
        view.report=std::make_shared<const ImportInspectionReport>(std::move(result));
        view.phase=InspectionPhase::Complete;
    }
    void run() override {
        for (;;) {
            std::optional<Request> request;
            std::stop_token stop;
            {
                QMutexLocker lock(&mutex);
                while (!queued && !closing) wake.wait(&mutex);
                if (!queued) break;
                request=std::move(queued); queued.reset(); view=*latest; stop=activeStop.get_token();
            }
            try {
                if (request->operation==InspectionOperation::InspectSource) execute(request->path,stop);
                else {
                    view.phase=request->operation==InspectionOperation::SaveBundle ? InspectionPhase::Saving : InspectionPhase::Loading;
                    publish();
                    if (options.beforeBundle) options.beforeBundle();
                    canceled(stop);
                    const InspectionBundleLimits limits{options.maximumInputBytes,options.maximumReportBytes};
                    if (request->operation==InspectionOperation::SaveBundle) {
                        check(bool(request->report),"No inspection result to save");
                        auto saving=options.bundleSave; saving.stop=stop;
                        const auto saved=saveInspectionBundle(request->path,*request->report,options.memory,limits,saving);
                        view.savedBytes=saved.bytes; view.savedDurability=saved.durability;
                    } else {
                        auto result=loadInspectionBundle(request->path,options.memory,limits,stop);
                        view.report=std::make_shared<const ImportInspectionReport>(std::move(result));
                    }
                    // Reopening a historical inspection never invents a child
                    // process; the report's PID remains originating provenance.
                    view.phase=InspectionPhase::Complete;
                }
            }
            catch (const ProjectError &error) {
                view.error=error.code();
                view.canceled=error.code()==ErrorCode::Canceled;
                if (view.messageId.empty()) view.messageId=id(error.code());
                view.phase=error.code()==ErrorCode::Canceled ? InspectionPhase::Canceled : InspectionPhase::Fault;
            } catch (const std::exception &) {
                view.error=ErrorCode::Io; view.messageId="import.worker_failure"; view.phase=InspectionPhase::Fault;
            }
            request.reset(); // Release the queued borrower before publishing retirement.
            view.busy=false;
            {
                QMutexLocker lock(&mutex); busy=false;
                latest=std::make_shared<const InspectionSnapshot>(view);
                if (closing) break;
            }
        }
        view.report.reset(); view.busy=false; view.closed=true; view.phase=InspectionPhase::Closed; publish();
    }
};
ImportInspectionController::ImportInspectionController(InspectionOptions options)
    : state_(std::make_unique<State>(std::move(options))) { state_->start(QThread::LowPriority); }
ImportInspectionController::~ImportInspectionController() { requestShutdown(); state_->wait(); }
Admission ImportInspectionController::submit(std::filesystem::path path) {
    return enqueue(InspectionOperation::InspectSource,std::move(path));
}
Admission ImportInspectionController::openBundle(std::filesystem::path path) {
    return enqueue(InspectionOperation::OpenBundle,std::move(path));
}
Admission ImportInspectionController::saveBundle(std::filesystem::path path,std::shared_ptr<const ImportInspectionReport> report) {
    return enqueue(InspectionOperation::SaveBundle,std::move(path),std::move(report));
}
Admission ImportInspectionController::enqueue(InspectionOperation operation,std::filesystem::path path,
    std::shared_ptr<const ImportInspectionReport> report) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing) return Admission::Closing;
    if (state_->busy || state_->sequence==std::numeric_limits<std::uint64_t>::max()) return Admission::Full;
    InspectionSnapshot next; next.job=state_->sequence+1; next.path=path; next.operation=operation; next.report=report;
    next.busy=true; next.phase=InspectionPhase::Queued;
    auto prepared=std::make_shared<const InspectionSnapshot>(std::move(next));
    std::stop_source nextStop;
    State::Request request{operation,std::move(path),std::move(report)};
    // Prepare allocating owners before changing admission; a construction
    // failure must not strand busy=true with no published request/wakeup.
    state_->activeStop=std::move(nextStop); state_->queued=std::move(request);
    state_->sequence=prepared->job; state_->busy=true;
    state_->latest=std::move(prepared); state_->wake.wakeOne();
    return Admission::Accepted;
}
void ImportInspectionController::requestCancel() noexcept {
    QMutexLocker lock(&state_->mutex); if (state_->busy) state_->activeStop.request_stop(); state_->wake.wakeOne();
}
void ImportInspectionController::requestShutdown() noexcept {
    QMutexLocker lock(&state_->mutex); state_->closing=true; state_->activeStop.request_stop(); state_->wake.wakeOne();
}
std::shared_ptr<const InspectionSnapshot> ImportInspectionController::snapshot() const {
    QMutexLocker lock(&state_->mutex); return state_->latest;
}
} // namespace soundcurrent::daw::ui
