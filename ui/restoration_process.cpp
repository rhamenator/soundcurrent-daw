// SPDX-License-Identifier: GPL-3.0-only
#include "restoration_process.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QFileInfo>
#include <QMutexLocker>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
namespace soundcurrent::daw::ui {
namespace {
void check(bool ok,const char *text,ErrorCode code=ErrorCode::InvalidState) {if(!ok)throw ProjectError(code,text);}
std::string hex(std::string_view s) {constexpr char d[]="0123456789abcdef";std::string out;out.reserve(s.size()*2);for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;}
std::string native(const std::filesystem::path &p){const auto u=p.u8string();return {reinterpret_cast<const char *>(u.data()),u.size()};}
std::string unhex(const nlohmann::json &j){check(j.is_string(),"Restoration record is not hex");const auto &s=j.get_ref<const std::string &>();check(s.size()<=assetRestorationRecordMaximumBytes*2 && s.size()%2==0,"Restoration record extent");
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
}
