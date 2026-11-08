// SPDX-License-Identifier: GPL-3.0-only
#include "restoration_process.hpp"
#include <soundcurrent/saved_restoration_record.hpp>
#include <soundcurrent/saved_restoration_permit.hpp>
#include <QCoreApplication>
#include <QProcess>
#include <QFileInfo>
#include <QMutexLocker>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <thread>
namespace soundcurrent::daw::ui {
namespace {
void check(bool ok,const char *text,ErrorCode code=ErrorCode::InvalidState) {if(!ok)throw ProjectError(code,text);}
std::string hex(std::string_view s) {constexpr char d[]="0123456789abcdef";std::string out;out.reserve(s.size()*2);for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;}
std::string native(const std::filesystem::path &p){const auto u=p.u8string();return {reinterpret_cast<const char *>(u.data()),u.size()};}
std::string unhex(const nlohmann::json &j,std::size_t maximum=assetRestorationRecordMaximumBytes){check(j.is_string(),"Restoration record is not hex");const auto &s=j.get_ref<const std::string &>();check(s.size()%2==0 && s.size()/2<=maximum,"Restoration record extent");
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw ProjectError(ErrorCode::InvalidState,"Restoration record hex");};
    std::string out;out.reserve(s.size()/2);for(std::size_t i=0;i<s.size();i+=2)out+=char((digit(s[i])<<4)|digit(s[i+1]));return out;}
nlohmann::json frame(std::string_view wire,std::initializer_list<std::string_view> keys){
    check(!wire.empty() && wire.size()<=32768,"Restoration reply extent");for(unsigned char c:wire)check(c>0&&c<128,"Restoration reply encoding");std::set<std::string> seen;
    const auto callback=[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json &v){check(depth<=1 && event!=nlohmann::json::parse_event_t::array_start,"Nested restoration reply");
        if(event==nlohmann::json::parse_event_t::key){const auto &k=v.get_ref<const std::string &>();check(std::find(keys.begin(),keys.end(),k)!=keys.end() && seen.insert(k).second,"Restoration reply keys");}return true;};
    auto j=nlohmann::json::parse(wire.begin(),wire.end(),callback);check(j.is_object()&&seen.size()==keys.size(),"Incomplete restoration reply");return j;
}
std::uint64_t integer(const nlohmann::json &j){check(j.is_number_unsigned(),"Restoration reply integer");return j.get<std::uint64_t>();}
bool boolean(const nlohmann::json &j){check(j.is_boolean(),"Restoration reply flag");return j.get<bool>();}
}
RestorationProcessResult runRestorationProcess(const AssetRestorationInput &input,const Session &session,
    const std::filesystem::path &root,RestorationOwnerContext context,const ControllerOptions &options,
    ResourceLedger memory,const std::atomic<bool> &closing,const std::atomic<bool> &cancel,QMutex &permissionMutex) {
    RestorationProcessResult out;QProcess child;ResourceLease bank;
    const auto begun=std::chrono::steady_clock::now();
    const auto timedOut=[&]{return std::chrono::steady_clock::now()-begun>=std::chrono::milliseconds(options.restorationDeadlineMilliseconds);};
    const auto poll=[&]{check(!closing.load(std::memory_order_acquire)&&!cancel.load(std::memory_order_acquire),"Restoration canceled",ErrorCode::Canceled);check(!timedOut(),"Restoration deadline",ErrorCode::Io);};
    try {
        poll();check(options.restorationDeadlineMilliseconds && input.checked && input.checked->selection && input.checked->replacement && input.checked->report,"Restoration input absent");
        constexpr std::size_t childCredit=64*1024*1024;
        bank=memory.reserve(childCredit+3*1024*1024); // Framing/parser/copy grants, not an RSS sandbox.
        const auto found=std::find_if(session.assets.begin(),session.assets.end(),[&](const Asset &a){return a.id==input.asset;});check(found!=session.assets.end(),"Restoration asset absent");
        const auto &asset=*found;const auto &selection=*input.checked->selection;
        RestorationOwnerView owner{session,root,context};const auto operation=Id::generate();out.operation=operation;
        RestorationDispatchTicket ticket(owner,asset.id,input.checked->replacement->ticket,*input.checked->report,input.checked->childPid,operation,selection.maximumBytes,memory);
        const auto planned=nlohmann::json({{"schema","sc-asset-restoration-v1"},{"phase",1},{"operation",operation.str()},{"asset",asset.id.str()},
            {"projectEpoch",context.checked.projectEpoch},{"modelRevision",context.checked.modelRevision},{"request",context.checked.request},
            {"relativePathHex",hex(asset.relativePath)},{"sourceSha256",asset.sha256},{"sourceBytes",input.checked->report->audio().sourceBytes},
            {"frames",asset.frames},{"rateHz",asset.sampleRate},{"channels",asset.layout.channels},{"layoutKind",static_cast<unsigned>(asset.layout.kind)},{"sourceRootsPersisted",false}}).dump();
        auto record=decodeAssetRestorationRecord(planned,memory);
        auto recordWire=encodeAssetRestorationRecord(record,AssetRestorationPhase::Planned,memory);
        auto original=input.checked->report->copyProtocol(memory);
        const auto request=nlohmann::json({{"protocol","sc-restoration-worker-v1"},{"project",session.id.str()},{"rootHex",hex(native(root))},
            {"sourceRootHex",hex(native(selection.root))},{"sourceLeafHex",hex(selection.relative)},{"recordHex",hex(recordWire.bytes())},
            {"checkReportHex",hex(original.bytes())},{"checkPid",input.checked->childPid},{"checkJob",context.checked.job},
            {"dispatchJob",context.dispatchJob},{"autosaveToken",context.autosaveToken},{"maximumBytes",selection.maximumBytes}}).dump()+"\n";
        check(request.size()<=65537,"Restoration request extent");
        QString program=options.restorationWorkerProgram.empty()?QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-asset-restoration-worker.exe"):
#else
            QStringLiteral("/sc-asset-restoration-worker"):
#endif
            QString::fromUtf8(native(options.restorationWorkerProgram).c_str());
        check(QFileInfo(program).isAbsolute(),"Restoration worker path must be absolute");
        child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&]{out.childPid=static_cast<std::size_t>(child.processId());});
        child.start(program,{QStringLiteral("--memory-bytes"),QString::number(childCredit),QStringLiteral("--maximum-source-bytes"),QString::number(selection.maximumBytes)},QIODevice::ReadWrite);
        check(child.write(request.data(),static_cast<qint64>(request.size()))==static_cast<qint64>(request.size()),"Restoration request channel",ErrorCode::Io);
        std::string output;output.reserve(65536);std::array<char,1024> errors{};std::size_t errorUsed=0;bool ready=false;
        auto drain=[&]{
            child.setReadChannel(QProcess::StandardError);
            while(child.bytesAvailable()){check(errorUsed<errors.size(),"Restoration stderr overflow",ErrorCode::ResourceLimit);const auto n=child.read(errors.data()+errorUsed,static_cast<qint64>(errors.size()-errorUsed));check(n>=0,"Restoration error channel",ErrorCode::Io);if(!n)break;errorUsed+=static_cast<std::size_t>(n);}
            child.setReadChannel(QProcess::StandardOutput);
            while(child.bytesAvailable()){check(output.size()<65536,"Restoration stdout overflow",ErrorCode::ResourceLimit);std::array<char,4096> chunk{};const auto n=child.read(chunk.data(),static_cast<qint64>(std::min(chunk.size(),65536-output.size())));check(n>=0,"Restoration output channel",ErrorCode::Io);if(!n)break;output.append(chunk.data(),static_cast<std::size_t>(n));}
        };
        while(child.state()!=QProcess::NotRunning){
            child.waitForReadyRead(20);drain();poll();
            if(!ready && output.find('\n')!=output.npos){
                const auto at=output.find('\n');std::string wire=output.substr(0,at);output.erase(0,at+1);
                if(options.restorationReadyHook)options.restorationReadyHook(wire,out.childPid);
                const auto j=frame(wire,{"protocol","workerPid","operation","recordHex"});check(j.at("protocol")=="sc-restoration-ready-v1" && out.childPid && integer(j.at("workerPid"))==out.childPid && j.at("operation")==operation.str(),"Restoration READY identity");
                auto prepared=decodeAssetRestorationRecord(unhex(j.at("recordHex")),memory);
                verifyAssetRestorationRecord(prepared,asset,{context.checked.projectEpoch,context.checked.modelRevision,context.checked.request},operation,AssetRestorationPhase::Planned,memory);
                if(options.restorationBeforePermit)options.restorationBeforePermit();
                poll();
                auto permit=encodeRestorationPublicationPermit(ticket,prepared,owner,out.childPid,memory);poll();
                const auto bytes=std::string(permit.bytes())+"\n";
                // Mark ambiguity before writing: an OS/channel error cannot prove
                // that no prefix reached this exact child.
                {QMutexLocker permission(&permissionMutex);
                 // Cancellation/close and permission issuance have one order.
                 // No callback or process wait runs while this mutex is held.
                 poll();out.permitSent=true;
                 check(child.write(bytes.data(),static_cast<qint64>(bytes.size()))==static_cast<qint64>(bytes.size()),"Restoration permit channel",ErrorCode::Io);
                 child.closeWriteChannel();ready=true;}

                if(options.restorationAfterPermit)options.restorationAfterPermit();
            }
        }
        drain();out.childExit=child.exitCode();bank.resize(bank.bytes()-childCredit); // Exact child is reaped.
        check(ready&&out.childPid&&child.exitStatus()==QProcess::NormalExit&&out.childExit==0&&!errorUsed,"Restoration child failed",ErrorCode::Io);
        if(options.restorationTerminalHook)options.restorationTerminalHook(output,out.childPid);
        check(!output.empty() && output.find('\n')==output.size()-1,"Restoration terminal framing");
        const auto j=frame(output,{"protocol","workerPid","operation","published","receiptPublished","postCommitFlushFailed","durability","recordHex"});
        check(j.at("protocol")=="sc-restoration-result-v1"&&integer(j.at("workerPid"))==out.childPid&&j.at("operation")==operation.str(),"Restoration terminal identity");
        auto terminal=decodeAssetRestorationRecord(unhex(j.at("recordHex")),memory);
        verifyAssetRestorationRecord(terminal,asset,{context.checked.projectEpoch,context.checked.modelRevision,context.checked.request},operation,AssetRestorationPhase::Published,memory);
        const auto published=boolean(j.at("published")),receipt=boolean(j.at("receiptPublished")),flushFailed=boolean(j.at("postCommitFlushFailed"));
        const auto durability=integer(j.at("durability"));
        check(published && (durability==1 || durability==2),"Restoration terminal outcome");
        out.published=published;out.receiptPublished=receipt;out.postCommitFlushFailed=flushFailed;out.durability=static_cast<unsigned>(durability);
#ifdef _WIN32
        check(out.durability==1,"Restoration Windows durability overstated");
#endif
        // Independent canonical bytes after reaping, before control ownership is
        // released. Hash is bounded/chunked; third-party audio decode stays child-only.
        ApprovedMediaRoot destination(root,memory,{1});auto canonical=destination.open(asset.relativePath,selection.maximumBytes);
        check(canonical.size()==input.checked->report->audio().sourceBytes,"Restored canonical extent",ErrorCode::MediaMismatch);
        const auto digest=canonical.digest({},[&]{check(!timedOut(),"Canonical verification deadline",ErrorCode::Io);});
        check(std::string_view(digest.data(),digest.size())==asset.sha256,"Restored canonical content",ErrorCode::MediaMismatch);
        canonical.verifyUnchanged();out.canonicalVerified=true;
    }catch(const ProjectError &e){out.error=e.code();}
    catch(const std::exception &){out.error=ErrorCode::Io;}
    if(child.state()!=QProcess::NotRunning){child.kill();while(child.state()!=QProcess::NotRunning)child.waitForFinished(20);out.childExit=child.exitCode();}
    if(!out.permitSent)out.published=false;
    out.needsReconciliation=out.permitSent&&!out.canonicalVerified;
    return out;
}
namespace {
void runSavedProcess(const RestorationInspectionRequest &original,
    const std::filesystem::path &root,const std::filesystem::path &source,
    const SavedRestorationSourceTicket &ticket,const WaveCheckSnapshot &checked,std::uint64_t preparationJob,
    std::uint64_t requestId,std::uint64_t job,const ControllerOptions &options,ResourceLedger memory,
    const std::atomic<bool> &closing,const std::atomic<bool> &cancel,SavedStagingCompletion &out,SavedPublicationProcessResult *publication,QMutex *permissionMutex) {
    out.request=requestId;out.job=job;out.preparationJob=preparationJob;
    QProcess child;ResourceLease bank;constexpr std::size_t childCredit=512*1024*1024;
    // These owners outlive every try-scope and all child failure cleanup.
    std::optional<RestorationAudit> auditOwner;
    std::optional<ApprovedMediaRoot> sourceRootOwner,destinationOwner;
    std::optional<ApprovedMediaFile> sourceOwner;
    std::optional<OwnedInspectionProtocol> controlOwner,reportOwner;
    std::optional<SavedRestorationDispatchTicket> dispatchOwner;
    const auto now=[&]()->std::uint64_t {return options.savedStagingNowMilliseconds?options.savedStagingNowMilliseconds():
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    const auto begun=now();std::uint64_t allowance=options.savedStagingDeadlineMilliseconds;
    const auto expired=[&]() noexcept {try{const auto current=now();return current<begun || current-begun>=allowance;}catch(...){return true;}};
    const auto poll=[&]{check(!closing.load(std::memory_order_acquire)&&!cancel.load(std::memory_order_acquire),"Saved staging canceled",ErrorCode::Canceled);
        check(!expired(),"Saved staging deadline",ErrorCode::Io);};
    std::stop_source workStop;const auto stop=workStop.get_token();std::jthread watchdog;
    try {
        poll();check(options.savedStagingDeadlineMilliseconds && requestId && job && preparationJob && checked.report && checked.childPid &&
            checked.childExit==0 && checked.phase==WaveCheckPhase::Complete && !checked.error && !checked.closed && !checked.replacement && checked.job,
            "Saved staging private checker is not complete");
        const auto sourceBytes=checked.report->audio().sourceBytes;
        check(sourceBytes && sourceBytes<=options.restorationInspectionMaximumBytes && allowance &&
            allowance<=options.savedStagingMaximumDeadlineMilliseconds,"Saved staging deadline policy invalid");
        const auto mib=sourceBytes/(1024*1024)+(sourceBytes%(1024*1024)!=0);
        const auto per=options.savedStagingMillisecondsPerMiB;const auto remaining=options.savedStagingMaximumDeadlineMilliseconds-allowance;
        const auto extra=per && mib>remaining/per?remaining:mib*static_cast<std::uint64_t>(per);
        allowance+=extra; // Saturated before multiplication; no untrusted extent overflow.
        poll();
        bank=memory.reserve(childCredit+3*1024*1024); // Logical child/frame admission, not an RSS sandbox.
        // The v1 child budgets are UTF-8 bytes, not Windows native code units.
        check(root.native().size()<=4096 && source.native().size()<=4096,"Saved staging native path extent");
        const auto rootBytes=native(root),sourceRootBytes=native(source.parent_path()),leafBytes=native(source.filename());
        const auto pathBytes=[](const std::string &bytes,std::size_t maximum){return !bytes.empty() && bytes.size()<=maximum && bytes.find('\0')==bytes.npos && validUtf8(bytes);};
        check(pathBytes(rootBytes,4096) && pathBytes(sourceRootBytes,4096) && pathBytes(leafBytes,1024),"Saved staging UTF-8 path extent");
        watchdog=std::jthread([&](std::stop_token retired){
            while(!retired.stop_requested()) {
                try {if(closing.load(std::memory_order_acquire)||cancel.load(std::memory_order_acquire)||expired()){workStop.request_stop();return;}}
                catch(...){workStop.request_stop();return;}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }); // Bridges cancel/close/deadline to every native audit/hash chunk, not just whole passes.
        const SavedRestorationSourceContext context{original.projectEpoch,original.modelRevision,original.request,checked.job};
        auto &audit=auditOwner.emplace(inspectSavedAssetRestoration(root,original.project,original.asset,original.operation,options.restorationInspectionMaximumBytes,memory,options.admission,stop));
        auto &control=controlOwner.emplace(ticket.copyControlProtocol(audit,*checked.report,context,checked.childPid,memory,stop));
        auto &report=reportOwner.emplace(checked.report->copyProtocol(memory,stop));
        auto &approved=sourceRootOwner.emplace(source.parent_path(),memory,ApprovedMediaLimits{1});
        auto &selected=sourceOwner.emplace(approved.openSelectedFilename(source.filename(),options.restorationInspectionMaximumBytes,stop));
         verifySavedRestorationSourceFile(ticket,audit,*checked.report,context,checked.childPid,selected,memory,stop,[&]{
             poll();if(options.savedStagingBeforeSourceChunk)options.savedStagingBeforeSourceChunk();poll();});
        const auto asset=std::find_if(audit.manifest().assets.begin(),audit.manifest().assets.end(),[&](const auto &a){return a.id==original.asset;});
        check(asset!=audit.manifest().assets.end(),"Saved stage asset disappeared");
        if(options.savedStagingBeforeDestination)options.savedStagingBeforeDestination();
        poll();auto &destination=destinationOwner.emplace(root,memory,ApprovedMediaLimits{2});
        ticket.verifyRootStamp(destination.directoryStamp(),memory);audit.reverify(stop); // No pathname reopen may reset the original identity.

        const auto operation=Id::generate();
        const SavedRestorationOwnerView owner{root,{context,preparationJob,requestId,job}};
        if(publication){check(permissionMutex,"Saved publication mutex absent");dispatchOwner.emplace(SavedRestorationDispatchTicket::bind(owner,audit,ticket,*checked.report,checked.childPid,operation,memory,stop));}
        auto requestPacket=nlohmann::json({{"protocol",publication?"sc-saved-restoration-worker-v2":"sc-saved-staging-worker-v1"},{"project",original.project.str()},{"asset",original.asset.str()},
            {"historyOperation",original.operation.str()},{"operation",operation.str()},{"rootHex",hex(rootBytes)},
            {"sourceRootHex",hex(sourceRootBytes)},{"sourceLeafHex",hex(leafBytes)},
            {"sourceControlHex",hex(control.bytes())},{"checkReportHex",hex(report.bytes())},{"checkPid",checked.childPid},
            {"maximumBytes",options.restorationInspectionMaximumBytes},{"projectEpoch",context.projectEpoch},{"modelRevision",context.modelRevision},
            {"sourceRequest",context.request},{"sourceJob",context.job},{"preparationJob",preparationJob},{"request",requestId},{"job",job},{"workerPid",UINT32_MAX}});
        check(requestPacket.dump().size()+1<=65537,"Saved staging request extent");
        poll();
        if(options.savedStagingBeforeSpawn)options.savedStagingBeforeSpawn();
        poll();
        const auto &configuredProgram=publication?options.savedPublicationWorkerProgram:options.savedStagingWorkerProgram;
        QString program=configuredProgram.empty()?QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            (publication?QStringLiteral("/sc-saved-restoration-live-worker.exe"):QStringLiteral("/sc-saved-restoration-worker.exe")):
#else
            (publication?QStringLiteral("/sc-saved-restoration-live-worker"):QStringLiteral("/sc-saved-restoration-worker")):
#endif
            QString::fromUtf8(native(configuredProgram).c_str());
        check(QFileInfo(program).isAbsolute(),"Saved staging worker must be absolute");
        child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&]{out.childPid=static_cast<std::size_t>(child.processId());out.stagingMayExist=true;});
        QStringList arguments{QStringLiteral("--memory-bytes"),QString::number(childCredit),QStringLiteral("--maximum-source-bytes"),QString::number(options.restorationInspectionMaximumBytes)};
        if(!options.savedStagingFixtureMode.empty()) {
            check(options.savedStagingFixtureMode.size()<=32,"Saved fixture mode extent");
            arguments<<QStringLiteral("--fixture-mode")<<QString::fromStdString(options.savedStagingFixtureMode);
        }
        out.launchAttempted=true;
        child.start(program,arguments,QIODevice::ReadWrite);
        if(options.savedStagingAfterLaunch)options.savedStagingAfterLaunch();
        poll();
        while(child.state()==QProcess::Starting){child.waitForStarted(20);
        poll();}
        check(child.state()==QProcess::Running && out.childPid && out.childPid<=UINT32_MAX,"Saved staging child did not start with a bounded native PID",ErrorCode::Io);
        out.operation=operation; // No operation observation exists until an actual child started.
        if(options.savedStagingAfterSpawn)options.savedStagingAfterSpawn();
        poll();
        const SavedRestorationRecordContext current{context,preparationJob,requestId,job,out.childPid};
        requestPacket["workerPid"]=out.childPid;
        const auto wire=requestPacket.dump()+"\n";
        out.requestSent=true;check(child.write(wire.data(),static_cast<qint64>(wire.size()))==static_cast<qint64>(wire.size()),"Saved staging input channel",ErrorCode::Io);
        if(!publication)child.closeWriteChannel(); // V1 refuses root access before exact EOF; V2 retains its pipe for permission.
        std::string output;output.reserve(32768);std::array<char,32768> errors{};std::size_t errorUsed=0;
        const auto drain=[&]{
            child.setReadChannel(QProcess::StandardError);
            while(child.bytesAvailable()){check(errorUsed<errors.size(),"Saved staging stderr overflow",ErrorCode::ResourceLimit);
                const auto n=child.read(errors.data()+errorUsed,static_cast<qint64>(errors.size()-errorUsed));check(n>=0,"Saved staging stderr",ErrorCode::Io);if(!n)break;errorUsed+=static_cast<std::size_t>(n);
                if(options.savedStagingDiagnosticHook)options.savedStagingDiagnosticHook(std::string_view(errors.data(),errorUsed));}
            child.setReadChannel(QProcess::StandardOutput);
            while(child.bytesAvailable()){check(output.size()<32768,"Saved staging stdout overflow",ErrorCode::ResourceLimit);std::array<char,4096> chunk{};
                const auto n=child.read(chunk.data(),static_cast<qint64>(std::min(chunk.size(),32768-output.size())));check(n>=0,"Saved staging stdout",ErrorCode::Io);if(!n)break;output.append(chunk.data(),static_cast<std::size_t>(n));}
        };
        bool readySeen=false;
        while(child.state()!=QProcess::NotRunning){child.waitForReadyRead(20);drain();poll();
            if(publication&&!readySeen&&output.find('\n')!=output.npos){
                const auto end=output.find('\n');auto readyWire=output.substr(0,end);output.erase(0,end+1);
                check(output.empty()&&!errorUsed,"Unexpected saved output before permission");
                if(options.savedPublicationReadyHook)options.savedPublicationReadyHook(readyWire,out.childPid);
                auto record=decodeSavedRestorationReady(readyWire,*dispatchOwner,owner,out.childPid,memory,stop);out.recordVerified=true;
                if(options.savedPublicationBeforePermit)options.savedPublicationBeforePermit();poll();
                auto permit=encodeSavedRestorationPublicationPermit(*dispatchOwner,record,owner,out.childPid,memory,stop);
                const auto permission=std::string(permit.bytes())+"\n";poll();
                // No callback, clock hook or wait under the actual cancel/close
                // mutex. Admission freezes context; these are real owner flags.
                {QMutexLocker lock(permissionMutex);
                 check(!closing.load(std::memory_order_acquire)&&!cancel.load(std::memory_order_acquire)&&!stop.stop_requested(),"Saved publication canceled",ErrorCode::Canceled);
                 publication->permitSent=true; // BEFORE any possible prefix.
                 check(child.write(permission.data(),static_cast<qint64>(permission.size()))==static_cast<qint64>(permission.size()),"Saved publication permit channel",ErrorCode::Io);
                 child.closeWriteChannel();readySeen=true;}
                if(options.savedPublicationAfterPermit)options.savedPublicationAfterPermit();
            }
        }
        out.childExit=child.exitCode();out.reaped=out.childPid!=0;out.normalExit=child.exitStatus()==QProcess::NormalExit;bank.resize(bank.bytes()-childCredit);drain();
        poll();
        check(out.reaped && child.exitStatus()==QProcess::NormalExit && out.childExit==0 && !errorUsed,"Saved staging child refused",ErrorCode::Io);
        if(options.savedStagingBeforeTerminal)options.savedStagingBeforeTerminal();
        poll();
        if(options.savedStagingTerminalHook)options.savedStagingTerminalHook(output,out.childPid);
        poll();
        check(!output.empty() && output.find('\n')==output.size()-1,"Saved staging terminal framing");
        if(publication){
            check(readySeen&&publication->permitSent,"Saved publication has no permission");
            const auto terminal=frame(output,{"protocol","workerPid","published","receiptPublished","postCommitFlushFailed","durability"});
            check(terminal.at("protocol")=="sc-saved-restoration-result-v2"&&integer(terminal.at("workerPid"))==out.childPid,"Saved publication terminal identity");
            const auto published=boolean(terminal.at("published")),receipt=boolean(terminal.at("receiptPublished")),flushFailed=boolean(terminal.at("postCommitFlushFailed"));
            const auto durability=integer(terminal.at("durability"));check(published&&(durability==1||durability==2),"Saved publication terminal outcome");
#ifdef _WIN32
            check(durability==1,"Saved Windows durability overstated");
#endif
            publication->published=true;publication->receiptPublished=receipt;publication->postCommitFlushFailed=flushFailed;
            publication->durability=static_cast<unsigned>(durability);publication->terminalVerified=true;
            const auto &audio=checked.report->audio();CheckedMediaBytes content{audio.sourceBytes,audio.sourceSha256};
            const auto namedRoot=[&]{ApprovedMediaRoot named(root,memory,{1});const auto currentRoot=named.directoryStamp(),heldRoot=destination.directoryStamp();
                check(currentRoot.identity[0]==heldRoot.identity[0]&&currentRoot.identity[1]==heldRoot.identity[1],"Saved canonical root replaced",ErrorCode::MediaMismatch);};
            namedRoot();
            auto canonical=destination.open(asset->relativePath,options.restorationInspectionMaximumBytes,stop);
            check(canonical.size()==content.bytes&&canonical.digest(stop,poll)==content.sha256,"Saved canonical bytes differ",ErrorCode::MediaMismatch);
            canonical.verifyUnchanged();poll();
            if(receipt){auto file=destination.open(operation.str()+"/receipt.json",savedRestorationRecordMaximumBytes,stop);
                std::string bytes(static_cast<std::size_t>(file.size()),'\0');file.readAt(0,std::span(bytes),stop);
                auto record=decodeSavedRestorationRecord(bytes,memory,stop);
                verifySavedRestorationRecord(record,original.project,*asset,content,current,operation,SavedRestorationPhase::Published,memory,stop);
                file.verifyUnchanged();poll();}
            namedRoot();canonical.verifyUnchanged();poll();publication->canonicalVerified=true;publication->receiptVerified=receipt;
        }else{
        const auto terminal=frame(output,{"protocol","workerPid","published","recordHex"});
        check(terminal.at("protocol")=="sc-saved-staging-result-v1" && integer(terminal.at("workerPid"))==out.childPid && !boolean(terminal.at("published")),"Saved staging terminal identity");
        auto record=decodeSavedRestorationRecord(unhex(terminal.at("recordHex"),savedRestorationRecordMaximumBytes),memory,stop);
        const auto &audio=checked.report->audio();CheckedMediaBytes content{audio.sourceBytes,audio.sourceSha256};
        verifySavedRestorationRecord(record,original.project,*asset,content,current,operation,SavedRestorationPhase::Planned,memory,stop);out.recordVerified=true;
        // Independent read after native exclusive writer handles have retired.
        // This is evidence, never native owner reconstruction/publication permission.
        auto staged=destination.open(operation.str()+"/media.partial",options.restorationInspectionMaximumBytes,stop);
        check(staged.size()==content.bytes && staged.digest(stop,poll)==content.sha256,"Saved staging bytes differ",ErrorCode::MediaMismatch);staged.verifyUnchanged();
        poll();
        auto intentFile=destination.open(operation.str()+"/intent.json",savedRestorationRecordMaximumBytes,stop);
        std::string intentBytes(static_cast<std::size_t>(intentFile.size()),'\0');intentFile.readAt(0,std::span(intentBytes),stop);
        auto intentRecord=decodeSavedRestorationRecord(intentBytes,memory,stop);verifySavedRestorationRecord(intentRecord,original.project,*asset,content,current,operation,SavedRestorationPhase::Planned,memory,stop);
        intentFile.verifyUnchanged();
        poll();out.stagedBytesVerified=true;
        }
        if(options.savedStagingAfterVerification)options.savedStagingAfterVerification(); // Final control-thread cancel/close ordering is checked by finishIo.
    }catch(const ProjectError &e){out.error=e.code();if(e.code()==ErrorCode::Canceled && !closing.load(std::memory_order_acquire) && !cancel.load(std::memory_order_acquire) && expired())out.error=ErrorCode::Io;}catch(const std::exception &){out.error=ErrorCode::Io;}
    const auto probe=[&](bool launchRetired){try{if(options.savedStagingRetirementProbe)options.savedStagingRetirementProbe(launchRetired);}catch(...){out.qualificationProbeFailed=true;if(!out.error)out.error=ErrorCode::Io;}};
    if(child.state()!=QProcess::NotRunning){probe(false);out.killRequested=true;child.kill();while(child.state()!=QProcess::NotRunning)child.waitForFinished(20);}
    out.launchRetired=out.launchAttempted && child.state()==QProcess::NotRunning;
    if(out.childPid){out.childExit=child.exitCode();out.reaped=true;out.normalExit=child.exitStatus()==QProcess::NormalExit;} // Actual QProcess retirement on every launched-child path.
    if(out.launchRetired)probe(true); // Pins also survive a retired Starting attempt with no confirmed PID.
    watchdog.request_stop();if(watchdog.joinable())watchdog.join();
    dispatchOwner.reset();sourceOwner.reset();sourceRootOwner.reset();destinationOwner.reset();auditOwner.reset();controlOwner.reset();reportOwner.reset();
    try{if(options.savedStagingAfterNativeRetirement)options.savedStagingAfterNativeRetirement();}catch(...){out.qualificationProbeFailed=true;if(!out.error)out.error=ErrorCode::Io;}
    if(out.error){out.recordVerified=false;out.stagedBytesVerified=false;}
    if(publication){
        try{poll();}catch(const ProjectError &e){if(!out.error)out.error=e.code();}
        catch(...){if(!out.error)out.error=ErrorCode::Io;}
        out.cancellationObserved=closing.load(std::memory_order_acquire)||cancel.load(std::memory_order_acquire);
        if(!publication->permitSent)publication->published=false;
        publication->needsReconciliation=publication->permitSent&&(!publication->terminalVerified||!publication->canonicalVerified||!publication->receiptVerified||publication->postCommitFlushFailed||out.error.has_value());
    }
}
} // namespace
SavedStagingCompletion runSavedStagingProcess(const RestorationInspectionRequest &original,const std::filesystem::path &root,
    const std::filesystem::path &source,const SavedRestorationSourceTicket &ticket,const WaveCheckSnapshot &checked,
    std::uint64_t preparationJob,std::uint64_t request,std::uint64_t job,const ControllerOptions &options,ResourceLedger memory,
    const std::atomic<bool> &closing,const std::atomic<bool> &cancel){
    SavedStagingCompletion out;
    runSavedProcess(original,root,source,ticket,checked,preparationJob,request,job,options,memory,closing,cancel,out,nullptr,nullptr);
    return out;
}
void runSavedPublicationProcessInto(SavedPublicationProcessResult &out,const RestorationInspectionRequest &original,const std::filesystem::path &root,
    const std::filesystem::path &source,const SavedRestorationSourceTicket &ticket,const WaveCheckSnapshot &checked,
    std::uint64_t preparationJob,std::uint64_t request,std::uint64_t job,const ControllerOptions &options,ResourceLedger memory,
    const std::atomic<bool> &closing,const std::atomic<bool> &cancel,QMutex &permissionMutex){
    out={}; // Before launch/mutation: facts thereafter belong to the caller.
    runSavedProcess(original,root,source,ticket,checked,preparationJob,request,job,options,memory,closing,cancel,out.staging,&out,&permissionMutex);
    {QMutexLocker lock(&permissionMutex);
     if(closing.load(std::memory_order_acquire)||cancel.load(std::memory_order_acquire)){
         out.staging.cancellationObserved=true;if(!out.staging.error)out.staging.error=ErrorCode::Canceled;
         if(out.permitSent)out.needsReconciliation=true;
     }}
    if(options.savedPublicationBeforeReturn)options.savedPublicationBeforeReturn(); // Qualification: caller facts survive an escaping exception.
}
SavedPublicationProcessResult runSavedPublicationProcess(const RestorationInspectionRequest &original,const std::filesystem::path &root,
    const std::filesystem::path &source,const SavedRestorationSourceTicket &ticket,const WaveCheckSnapshot &checked,
    std::uint64_t preparationJob,std::uint64_t request,std::uint64_t job,const ControllerOptions &options,ResourceLedger memory,
    const std::atomic<bool> &closing,const std::atomic<bool> &cancel,QMutex &permissionMutex){
    SavedPublicationProcessResult out;
    runSavedPublicationProcessInto(out,original,root,source,ticket,checked,preparationJob,request,job,options,memory,closing,cancel,permissionMutex);
    return out;
}

SavedStagingCompletion runSavedManifestStagingProcess(const SavedManifestSourceRequest &original,
    const std::filesystem::path &root,const std::filesystem::path &source,
    const SavedManifestSourceTicket &ticket,std::string_view originalControl,const WaveCheckSnapshot &checked,
    std::uint64_t preparationJob,std::uint64_t requestId,std::uint64_t job,const ControllerOptions &options,
    ResourceLedger memory,const std::atomic<bool> &closing,const std::atomic<bool> &cancel) {
    SavedStagingCompletion out;out.request=requestId;out.job=job;out.preparationJob=preparationJob;
    QProcess child;ResourceLease bank;constexpr std::size_t childCredit=512*1024*1024;
    // Native owners outlive ALL exception scopes and actual process retirement.
    std::optional<SavedManifestAudit> auditOwner;
    std::optional<ApprovedMediaRoot> sourceRootOwner;
    std::optional<ApprovedMediaFile> sourceOwner;
    std::optional<SavedManifestDispatchTicket> dispatchOwner;
    std::optional<OwnedInspectionProtocol> reportOwner;
    const auto now=[&]()->std::uint64_t{return options.savedStagingNowMilliseconds?options.savedStagingNowMilliseconds():
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());};
    const auto begun=now();std::uint64_t allowance=options.savedStagingDeadlineMilliseconds;
    const auto expired=[&]() noexcept{try{const auto current=now();return current<begun||current-begun>=allowance;}catch(...){return true;}};
    const auto poll=[&]{check(!closing.load(std::memory_order_acquire)&&!cancel.load(std::memory_order_acquire),"Manifest staging canceled",ErrorCode::Canceled);
        check(!expired(),"Manifest staging deadline",ErrorCode::Io);};
    std::stop_source workStop;const auto stop=workStop.get_token();std::jthread watchdog;
    try {
        poll();check(preparationJob&&requestId&&job&&checked.job&&checked.childPid&&checked.childExit==0&&checked.report&&
            checked.phase==WaveCheckPhase::Complete&&!checked.busy&&!checked.closed&&!checked.error&&!checked.timedOut&&!checked.replacement&&checked.selection,
            "Manifest staging private checker is not complete");
        const auto bytes=checked.report->audio().sourceBytes;
        check(bytes&&bytes<=options.restorationInspectionMaximumBytes&&allowance&&
            allowance<=options.savedStagingMaximumDeadlineMilliseconds,"Manifest staging trusted policy invalid");
        const auto mib=bytes/(1024*1024)+(bytes%(1024*1024)!=0),remaining=options.savedStagingMaximumDeadlineMilliseconds-allowance;
        const auto per=options.savedStagingMillisecondsPerMiB;
        allowance+=per&&mib>remaining/per?remaining:mib*static_cast<std::uint64_t>(per);
        bank=memory.reserve(childCredit+3*1024*1024); // Admission, not a process RSS sandbox.
        check(root.is_absolute()&&source.is_absolute()&&root.native().size()<=4096&&source.native().size()<=4096,
            "Manifest staging explicit native path extent");
        const auto rootBytes=native(root),sourceRootBytes=native(source.parent_path()),leafBytes=native(source.filename());
        const auto pathBytes=[](const std::string &v,std::size_t maximum){return !v.empty()&&v.size()<=maximum&&v.find('\0')==v.npos&&validUtf8(v);};
        check(pathBytes(rootBytes,4096)&&pathBytes(sourceRootBytes,4096)&&pathBytes(leafBytes,1024),"Manifest staging UTF-8 path extent");
        check(memory.owns(checked.selection->lease)&&checked.selection->selectedFilename&&
            checked.selection->root==source.parent_path()&&checked.selection->relative==leafBytes&&
            checked.report->relative()==leafBytes&&bytes<=checked.selection->maximumBytes,
            "Manifest staging differs from original private checker selection",ErrorCode::MediaMismatch);
        watchdog=std::jthread([&](std::stop_token retired){while(!retired.stop_requested()){
            if(closing.load(std::memory_order_acquire)||cancel.load(std::memory_order_acquire)||expired()){workStop.request_stop();return;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));}});
        const SavedRestorationSourceContext context{original.projectEpoch,original.modelRevision,original.request,checked.job};
        const SavedRestorationOwnerView owner{root,{context,preparationJob,requestId,job}};
        auto &audit=auditOwner.emplace(inspectSavedManifestAsset(root,original.project,original.asset,
            options.restorationInspectionMaximumBytes,memory,options.admission,stop));
        auto &sourceRoot=sourceRootOwner.emplace(source.parent_path(),memory,ApprovedMediaLimits{1});
        auto &selected=sourceOwner.emplace(sourceRoot.openSelectedFilename(source.filename(),options.restorationInspectionMaximumBytes,stop));
        ticket.verifyFile(audit,*checked.report,context,checked.childPid,selected,memory,stop,[&]{
            poll();if(options.savedStagingBeforeSourceChunk)options.savedStagingBeforeSourceChunk();poll();});
        if(options.savedStagingBeforeDestination)options.savedStagingBeforeDestination();poll();
        const auto operation=Id::generate();
        auto &dispatch=dispatchOwner.emplace(SavedManifestDispatchTicket::bind(owner,audit,ticket,selected,originalControl,
            *checked.report,checked.childPid,operation,memory,stop));
        auto &report=reportOwner.emplace(checked.report->copyProtocol(memory,stop));
        auto packet=nlohmann::json({{"protocol","sc-saved-manifest-staging-worker-v1"},{"project",original.project.str()},
            {"asset",original.asset.str()},{"operation",operation.str()},{"rootHex",hex(rootBytes)},
            {"sourceRootHex",hex(sourceRootBytes)},{"sourceLeafHex",hex(leafBytes)},{"sourceControlHex",hex(originalControl)},
            {"checkReportHex",hex(report.bytes())},{"checkPid",checked.childPid},{"maximumBytes",options.restorationInspectionMaximumBytes},
            {"projectEpoch",context.projectEpoch},{"modelRevision",context.modelRevision},{"sourceRequest",context.request},
            {"sourceJob",context.job},{"preparationJob",preparationJob},{"request",requestId},{"job",job},{"workerPid",UINT32_MAX}});
        check(packet.dump().size()+1<=65537,"Manifest staging request extent");
        const auto &configured=options.savedManifestStagingWorkerProgram;
        const QString program=configured.empty()?QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-saved-manifest-worker.exe"):
#else
            QStringLiteral("/sc-saved-manifest-worker"):
#endif
            QString::fromUtf8(native(configured).c_str());
        check(QFileInfo(program).isAbsolute(),"Manifest staging worker must be absolute");
        if(options.savedStagingBeforeSpawn)options.savedStagingBeforeSpawn();poll();
        dispatch.verifyBeforeLaunch(owner,audit,ticket,selected,*checked.report,checked.childPid,memory,stop);
        child.setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(&child,&QProcess::started,&child,[&]{out.childPid=static_cast<std::size_t>(child.processId());out.stagingMayExist=true;});
        out.launchAttempted=true;
        child.start(program,{QStringLiteral("--memory-bytes"),QString::number(childCredit),
            QStringLiteral("--maximum-source-bytes"),QString::number(options.restorationInspectionMaximumBytes)},QIODevice::ReadWrite);
        if(options.savedStagingAfterLaunch)options.savedStagingAfterLaunch();poll();
        while(child.state()==QProcess::Starting){child.waitForStarted(20);poll();}
        check(child.state()==QProcess::Running&&out.childPid&&out.childPid<=UINT32_MAX,"Manifest staging child did not start",ErrorCode::Io);
        out.operation=operation;
        if(options.savedStagingAfterSpawn)options.savedStagingAfterSpawn();poll();
        dispatch.verifyBeforeLaunch(owner,audit,ticket,selected,*checked.report,checked.childPid,memory,stop);
        packet["workerPid"]=out.childPid;const auto wire=packet.dump()+"\n";
        if(options.savedManifestStagingBeforeInput)options.savedManifestStagingBeforeInput();poll();
        out.requestSent=true;
        check(child.write(wire.data(),static_cast<qint64>(wire.size()))==static_cast<qint64>(wire.size()),"Manifest staging request channel",ErrorCode::Io);
        if(options.savedManifestStagingBeforeEof)options.savedManifestStagingBeforeEof();poll();
        child.closeWriteChannel(); // The child refuses filesystem work until exact EOF.
        std::string output;output.reserve(32768);std::array<char,32768> errors{};std::size_t errorUsed=0;
        const auto drain=[&]{
            child.setReadChannel(QProcess::StandardError);
            while(child.bytesAvailable()){
                check(errorUsed<errors.size(),"Manifest staging stderr overflow",ErrorCode::ResourceLimit);
                const auto n=child.read(errors.data()+errorUsed,static_cast<qint64>(errors.size()-errorUsed));
                check(n>=0,"Manifest staging stderr",ErrorCode::Io);if(!n)break;errorUsed+=static_cast<std::size_t>(n);
                if(options.savedStagingDiagnosticHook)options.savedStagingDiagnosticHook(std::string_view(errors.data(),errorUsed));}
            child.setReadChannel(QProcess::StandardOutput);
            while(child.bytesAvailable()){
                check(output.size()<32768,"Manifest staging stdout overflow",ErrorCode::ResourceLimit);std::array<char,4096> chunk{};
                const auto n=child.read(chunk.data(),static_cast<qint64>(std::min(chunk.size(),32768-output.size())));
                check(n>=0,"Manifest staging stdout",ErrorCode::Io);if(!n)break;output.append(chunk.data(),static_cast<std::size_t>(n));}
        };
        while(child.state()!=QProcess::NotRunning){child.waitForReadyRead(20);drain();poll();}
        out.childExit=child.exitCode();out.reaped=out.childPid!=0;out.normalExit=child.exitStatus()==QProcess::NormalExit;
        bank.resize(bank.bytes()-childCredit);drain();poll();
        check(out.reaped&&out.normalExit==true&&out.childExit==0&&!errorUsed,"Manifest staging child refused",ErrorCode::Io);
        if(options.savedStagingBeforeTerminal)options.savedStagingBeforeTerminal();poll();
        if(options.savedStagingTerminalHook)options.savedStagingTerminalHook(output,out.childPid);poll();
        check(!output.empty()&&output.find('\n')==output.size()-1,"Manifest staging terminal framing");
        const auto terminal=frame(output,{"protocol","workerPid","published","recordHex"});
        check(terminal.at("protocol")=="sc-saved-manifest-staging-result-v1"&&integer(terminal.at("workerPid"))==out.childPid&&
            !boolean(terminal.at("published")),"Manifest staging terminal identity");
        auto record=decodeSavedRestorationRecord(unhex(terminal.at("recordHex"),savedRestorationRecordMaximumBytes),memory,stop);
        dispatch.inspectStaged(owner,audit,ticket,selected,*checked.report,checked.childPid,out.childPid,record,memory,stop,poll);
        out.recordVerified=true;out.stagedBytesVerified=true;
        if(options.savedStagingAfterVerification)options.savedStagingAfterVerification();poll();
    }catch(const ProjectError &e){out.error=e.code();if(e.code()==ErrorCode::Canceled&&!closing.load(std::memory_order_acquire)&&
        !cancel.load(std::memory_order_acquire)&&expired())out.error=ErrorCode::Io;}
    catch(...){out.error=ErrorCode::Io;}
    const auto probe=[&](bool retired){try{if(options.savedStagingRetirementProbe)options.savedStagingRetirementProbe(retired);}
        catch(...){out.qualificationProbeFailed=true;if(!out.error)out.error=ErrorCode::Io;}};
    if(child.state()!=QProcess::NotRunning){probe(false);out.killRequested=true;child.kill();while(child.state()!=QProcess::NotRunning)child.waitForFinished(20);}
    out.launchRetired=out.launchAttempted&&child.state()==QProcess::NotRunning;
    if(out.childPid){out.childExit=child.exitCode();out.reaped=true;out.normalExit=child.exitStatus()==QProcess::NormalExit;}
    if(out.launchRetired)probe(true);
    watchdog.request_stop();if(watchdog.joinable())watchdog.join();
    dispatchOwner.reset();sourceOwner.reset();sourceRootOwner.reset();auditOwner.reset();reportOwner.reset();
    try{if(options.savedStagingAfterNativeRetirement)options.savedStagingAfterNativeRetirement();}
    catch(...){out.qualificationProbeFailed=true;if(!out.error)out.error=ErrorCode::Io;}
    out.cancellationObserved=closing.load(std::memory_order_acquire)||cancel.load(std::memory_order_acquire)||out.error==ErrorCode::Canceled;
    if(out.cancellationObserved&&!out.error)out.error=ErrorCode::Canceled;
    if(out.error){out.recordVerified=false;out.stagedBytesVerified=false;}
    return out;
}
}
