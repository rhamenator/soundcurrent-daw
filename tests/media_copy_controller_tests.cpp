// SPDX-License-Identifier: GPL-3.0-only
#include "media_copy_controller.hpp"
#include "import_inspection_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QTemporaryDir>
#include <QThread>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <atomic>
#include <algorithm>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool good,const char *text) {++checks;if(!good) throw std::runtime_error(text);}
template<class F> void wait(F fn) {QElapsedTimer clock;clock.start();while(!fn()) {if(clock.elapsed()>15000) throw std::runtime_error("Copy controller deadline");QTest::qWait(2);}}
template<class F> void refuses(F fn) {bool refused=false;try {fn();} catch(const ProjectError &) {refused=true;}check(refused,"Invalid copy protocol admitted");}
auto finish(MediaCopyController &c) {wait([&]{return !c.snapshot()->busy;});return c.snapshot();}
void write(const std::filesystem::path &p,std::string_view bytes) {std::ofstream out(p,std::ios::binary);out<<bytes;out.close();check(bool(out),"Cannot write owned copy fixture");}
std::string read(const std::filesystem::path &p) {std::ifstream in(p,std::ios::binary);check(bool(in),"Cannot read owned copy fixture");return {std::istreambuf_iterator<char>(in),{}};}
void put(std::string &s,unsigned n,unsigned count) {for(unsigned i=0;i<count;++i) s+=char((n>>(8*i))&255);}
std::string wave() {
    std::string s="RIFF";put(s,44,4);s+="WAVEfmt ";put(s,16,4);put(s,1,2);put(s,1,2);put(s,48000,4);put(s,96000,4);put(s,2,2);put(s,16,2);s+="data";put(s,8,4);
    for(unsigned n:{0,1,32767,32768}) put(s,n,2);
    return s;
}
struct Release {std::atomic<bool> &flag;~Release(){flag=true;}};
std::string utf8(const std::filesystem::path &p) {const auto s=p.u8string();return {reinterpret_cast<const char *>(s.data()),s.size()};}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try {
#ifdef _WIN32
        // Windows uses the native temporary directory rather than /tmp.
        QTemporaryDir native;const auto base=utf8Path(native.path().toUtf8().toStdString());check(native.isValid(),"No native copy folder");
#else
        QTemporaryDir tmp(QStringLiteral("/tmp/sc-copy-été-XXXXXX"));
        const auto base=utf8Path(tmp.path().toUtf8().toStdString());check(tmp.isValid(),"No copy folder");
#endif
        const auto root=base/utf8Path("été-Κиїв");std::filesystem::create_directory(root);
        const auto destination=root/"copies";std::filesystem::create_directory(destination);
        const std::string name="été-Κиїв.wav";const auto media=root/utf8Path(name);const auto original=wave();write(media,original);
        const auto file=root/"project.rpp";
        const auto project="<REAPER_PROJECT 0.1 7.82\n <TRACK\n  <ITEM\n   <SOURCE WAVE\n    FILE \""+name+"\"\n   >\n  >\n >\n>\n";write(file,project);
        ResourceLedger memory(128*1024*1024,"Copy acceptance");
        {
            InspectionOptions io;io.memory=memory;ImportInspectionController inspector(io);inspector.submit(file);wait([&]{return !inspector.snapshot()->busy;});
            const auto inspection=inspector.snapshot()->report;check(bool(inspection),"Actual inspector failed");
            const auto property=static_cast<std::size_t>(std::find_if(inspection->properties().begin(),inspection->properties().end(),[](const ImportProperty &p){return p.id==ImportPropertyId::SourceFile;})-inspection->properties().begin());
            WaveCheckOptions wo;wo.memory=memory;WaveCheckController checker(wo);checker.submit(root,name,1048576);wait([&]{return !checker.snapshot()->busy;});
            auto checked=checker.snapshot();check(checked->report && checked->phase==WaveCheckPhase::Complete,"Actual WAVE checker failed");
            MediaCopyOptions options;options.memory=memory;std::shared_ptr<const MediaCopySnapshot> borrowed;
            {
                MediaCopyController c(options);check(c.copy(inspection,property,checked,destination)==Admission::Accepted,"Checked copy not admitted");borrowed=finish(c);
                check(borrowed->phase==MediaCopyPhase::Committed && borrowed->childPid && borrowed->childExit==0 && borrowed->provenance,"Actual copy child did not commit");
                const auto id=borrowed->selection->operation;
                check(borrowed->provenance->data().originalReference==name && borrowed->provenance->data().sourceProperty==property,"Checked occurrence not bound");
                check(read(destination/id.str()/"media.wav")==original && read(media)==original && read(file)==project,"Copy altered source or bytes");
                check(!std::filesystem::exists(destination/"project.json"),"Copy published a Session");
                check(c.recover(destination,id,1048576)==Admission::Accepted,"Explicit recovery refused");const auto recovered=finish(c);
                check(recovered->phase==MediaCopyPhase::Committed && recovered->childPid && recovered->childExit==0 && !recovered->durability,"Fresh recovery invented historical durability");
                check(c.clearResult() && !c.snapshot()->selection,"Clear retained request");
                c.requestShutdown();wait([&]{return c.snapshot()->closed;});
            }
            check(borrowed->provenance->data().audio.frames==4,"Borrower lost result after facade retirement");borrowed.reset();
            {
                std::atomic<bool> reached=false,release=false,offGui=false;MediaCopyOptions held=options;
                held.beforeSpawn=[&]{offGui=QThread::currentThread()!=app.thread();reached=true;while(!release) QThread::msleep(1);};
                MediaCopyController c(held);Release guard{release};check(c.copy(inspection,property,checked,destination)==Admission::Accepted,"Held copy refused");
                wait([&]{return reached.load();});const auto id=c.snapshot()->selection->operation;
                check(offGui && c.copy(inspection,property,checked,destination)==Admission::Full && !c.clearResult(),"Copy single flight/off-GUI contract failed");
                check(memory.usage().reservedBytes>64*1024*1024,"Child not admitted before spawn");c.requestCancel();release=true;const auto s=finish(c);
                check(s->phase==MediaCopyPhase::Canceled && !s->childPid && !std::filesystem::exists(destination/id.str()),"Pre-spawn cancel mutated destination");
            }
            {
                auto changed=original;changed.back()^=1;write(media,changed);MediaCopyController c(options);c.copy(inspection,property,checked,destination);const auto s=finish(c);
                check(s->phase==MediaCopyPhase::RecoveryRequired && s->error==ErrorCode::MediaMismatch && s->childPid && s->childExit!=0,"Changed checked bytes were copied or treated as definite publication absence");
                check(!std::filesystem::exists(destination/s->selection->operation.str()),"Changed source mutated destination");write(media,original);
                c.recover(destination,s->selection->operation,1048576);check(finish(c)->phase==MediaCopyPhase::NoEvidence,"Changed-source refusal recovery not explicit");
            }
            {
                const auto changedProject=root/"different-inspection.rpp";write(changedProject,project+"\n");
                inspector.submit(changedProject);wait([&]{return !inspector.snapshot()->busy;});const auto different=inspector.snapshot()->report;
                check(different && different->sha256()!=inspection->sha256(),"Different actual inspection missing");
                const auto bundle=root/"different.scinspect";saveInspectionBundle(bundle,*different,memory);const auto bytes=read(bundle);
                MediaCopyOptions changed=options;changed.afterRequestPrepared=[&](const MediaCopyRequestData &r){write(utf8Path(r.bundle),bytes);};
                MediaCopyController c(changed);c.copy(inspection,property,checked,destination);const auto s=finish(c);
                check(s->childPid && s->childExit!=0 && s->phase==MediaCopyPhase::RecoveryRequired &&
                    !std::filesystem::exists(destination/s->selection->operation.str()),"Valid changed inspection was copied against an older approval");
            }
            {
                ResourceLedger other(128*1024*1024);MediaCopyOptions bad=options;bad.memory=other;MediaCopyController c(bad);
                refuses([&]{c.copy(inspection,property,checked,destination);});check(!c.snapshot()->busy && !c.snapshot()->selection,"Unadmitted copy queued");
            }
            { // Request codec has no implicit historical root on recovery.
                auto origin=bindMediaProvenance(*inspection,property,*checked->report,MediaSelectionKind::ApprovedReference,Id::generate(),memory);
                auto saved=encodeMediaProvenance(origin,MediaReceiptPhase::Planned,memory);MediaCopyRequestData d(origin.data().operation);
                d.bundle=utf8(root/"snapshot.scinspect");d.root=utf8(root);d.destination=utf8(destination);d.maximumBytes=1048576;d.expectedReceipt=saved.bytes();
                auto encoded=encodeMediaCopyRequest(d,memory);const auto valid=nlohmann::json::parse(encoded.bytes());
                for(unsigned mode=0;mode<12;++mode) {
                    auto bad=valid;
                    switch(mode) {
                    case 0:bad["schema"]="wrong";break;case 1:bad["unknown"]=1;break;case 2:bad.erase("root");break;
                    case 3:bad["maximumBytes"]=-1;break;case 4:bad["maximumBytes"]=1.0;break;case 5:bad["maximumBytes"]=0;break;
                    case 6:bad["destination"]="relative";break;case 7:bad["recover"]=true;break;case 8:bad["selectedFilename"]=true;break;
                    case 9:bad["expected"]="{}";break;case 10:bad["operation"]=Id::generate().str();break;case 11:bad["root"]=nlohmann::json::array();break;
                    }
                    const auto before=memory.usage();refuses([&]{decodeMediaCopyRequest(bad.dump(),memory);});
                    check(memory.usage().owners==before.owners && memory.usage().reservedBytes==before.reservedBytes,"Refused codec leaked credit");
                }
                auto duplicate=std::string(encoded.bytes());duplicate.insert(1,"\"recover\":false,");refuses([&]{decodeMediaCopyRequest(duplicate,memory);});
                refuses([&]{decodeMediaCopyRequest(std::string(mediaCopyRequestMaximum+1,'x'),memory);});
            }
            for(unsigned mode=0;mode<12;++mode) {
                MediaCopyOptions corrupt=options;std::atomic<bool> exercised=false;
                corrupt.afterChild=[&,mode](std::string &raw,std::size_t pid) {
                    auto j=nlohmann::json::parse(raw);
                    switch(mode) {
                    case 0:j["workerPid"]=pid+1;break;case 1:j["operation"]=Id::generate().str();break;
                    case 2:j["committed"]=false;break;case 3:j["sessionAssetPublished"]=true;break;
                    case 4:j["phase"]=-1;break;case 5:j["durability"]=3;break;
                    case 6:j["unknown"]=1;break;case 7:j["receipt"]="{}";break;
                    case 8:{auto r=nlohmann::json::parse(j["receipt"].get<std::string>());r["sourceProperty"]=999;j["receipt"]=r.dump();break;}
                    case 9:j["postCommitFlushFailed"]=true;j["durability"]=2;break;
                    case 10:j["receipt"]=nlohmann::json::array();break;case 11:raw.insert(raw.find('{')+1,"\"phase\":2,");exercised=true;return;
                    }
                    raw=j.dump();exercised=true;
                };
                MediaCopyController c(corrupt);c.copy(inspection,property,checked,destination);const auto s=finish(c);
                check(exercised && s->childPid && s->childExit==0 && s->phase==MediaCopyPhase::RecoveryRequired && !s->provenance,"Corrupt actual-child reply admitted");
                MediaCopyController recovery(options);recovery.recover(destination,s->selection->operation,1048576);
                check(finish(recovery)->phase==MediaCopyPhase::Committed,"Actual published copy lost after report rejection");
            }
            const auto probe=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
                QStringLiteral("/sc-media-copy-lifecycle-probe.exe");
#else
                QStringLiteral("/sc-media-copy-lifecycle-probe");
#endif
            for(unsigned scenario=0;scenario<5;++scenario) {
                WaveCheckOptions fixture=wo;WaveCheckController small(fixture);small.submit(root,name,scenario==4 ? 999991:999991+scenario);wait([&]{return !small.snapshot()->busy;});
                MediaCopyOptions test=options;test.program=probe;test.deadlineMilliseconds=scenario==4 ? 400:5000;
                MediaCopyController c(test);check(c.copy(inspection,property,small.snapshot(),destination)==Admission::Accepted,"Native boundary copy refused");
                const auto id=c.snapshot()->selection->operation;
                if(scenario<2) {
                    wait([&]{return c.snapshot()->childPid && std::filesystem::exists(destination/(id.str()+".ready"));});
                    c.requestCancel();
                }
                const auto s=finish(c);check(s->phase==MediaCopyPhase::RecoveryRequired && s->childPid && s->childExit.has_value(),"Stopped/lost-report child claimed definite copy absence");
                if(scenario<2) check(s->canceled,"Boundary cancel not retained");
                if(scenario==2) check(s->error==ErrorCode::ResourceLimit,"Actual output flood not bounded");
                if(scenario==4) check(s->timedOut,"Actual precommit deadline not retained");
                MediaCopyController recovery(options);recovery.recover(destination,id,1048576);const auto r=finish(recovery);
                check(r->phase==(scenario==0 || scenario==4 ? MediaCopyPhase::Planned:MediaCopyPhase::Committed),"Fresh recovery cannot distinguish pre/post publication interruption");
                check(read(destination/id.str()/"media.wav")==original,"Interrupted copy bytes changed");
            }
            {
                const auto before=memory.usage();memory.configure(before.reservedBytes+20000);MediaCopyController c(options);
                c.copy(inspection,property,checked,destination);const auto s=finish(c);check(s->phase==MediaCopyPhase::Fault && s->error==ErrorCode::ResourceLimit && !s->childPid,"Unadmitted work launched a child");
                check(!std::filesystem::exists(destination/s->selection->operation.str()),"Unadmitted work mutated destination");memory.configure(128*1024*1024);
            }
            {
                MediaCopyOptions absent=options;absent.program=QCoreApplication::applicationDirPath()+QStringLiteral("/missing-copy-program");
                MediaCopyController c(absent);c.copy(inspection,property,checked,destination);const auto s=finish(c);
                check(!s->childPid && s->phase==MediaCopyPhase::Fault,"Failed start claims destination access");
            }
            check(read(media)==original && read(file)==project,"Acceptance changed original sources");
        }
        check(memory.usage().owners==0 && memory.usage().reservedBytes==0,"Retired copy/check/inspection credits leaked");
        std::cout<<"PASS: "<<checks<<" actual checked-copy/controller checks; frozen source, PID/receipt, pre/post publication interruption, recovery and retirement; no audio device\n";return 0;
    } catch(const std::exception &e) {std::cerr<<"Copy acceptance failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
