// SPDX-License-Identifier: GPL-3.0-only
#include "wave_check_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool b,const char *text) {++checks;if (!b) throw std::runtime_error(text);}
template<class F> void wait(F fn) {QElapsedTimer clock;clock.start();while (!fn()) {if (clock.elapsed()>10000) throw std::runtime_error("Media controller test deadline");QTest::qWait(2);}}
auto finish(WaveCheckController &c) {wait([&]{return !c.snapshot()->busy;});return c.snapshot();}
void retired(ResourceLedger m) {check(m.usage().reservedBytes==0 && m.usage().owners==0,"Media controller credit leaked");}
void put(std::string &s,unsigned n,unsigned count) {for (unsigned i=0;i<count;++i) s+=static_cast<char>((n>>(8*i))&255);}
std::string wave() {
    std::string s="RIFF";put(s,44,4);s+="WAVEfmt ";put(s,16,4);put(s,1,2);put(s,1,2);put(s,48000,4);put(s,96000,4);put(s,2,2);put(s,16,2);s+="data";put(s,8,4);
    for (const unsigned n:{0,1,32767,32768}) put(s,n,2);
    return s;
}
struct Release {std::atomic<bool> &flag;~Release(){flag=true;}};
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir tmp;check(tmp.isValid(),"No owned media test folder");const auto root=utf8Path(tmp.path().toUtf8().toStdString());
        const std::string name="été-Κиїв.wav";const auto file=root/utf8Path(name);const auto original=wave();
        {std::ofstream out(file,std::ios::binary);out<<original;check(bool(out),"Cannot write original WAVE fixture");}
        std::string originalHash;
        ResourceLedger memory(64*1024*1024,"Media controller acceptance");std::shared_ptr<const WaveCheckSnapshot> held;
        {
            WaveCheckOptions options;options.memory=memory;WaveCheckController c(options);
            check(c.submit(root,name)==Admission::Accepted,"Media selection not admitted");held=finish(c);
            check(held->phase==WaveCheckPhase::Complete && held->childPid && held->childExit==0 && held->report,"Real media child result incomplete");
            originalHash.assign(held->report->audio().sourceSha256.begin(),held->report->audio().sourceSha256.end());
            check(held->report->workerPid()==held->childPid && held->report->relative()==name,"Child PID/reference mismatched");
            check(held->report->audio().frames==4 && held->report->audio().channels==1 && held->report->audio().peak==1,"Actual normalized file samples/metadata wrong");
            check(held->report->ownedBy(memory) && memory.usage().reservedBytes==held->report->chargedBytes()+held->selection->lease.bytes(),"Transient child/parser credit retained");
            check(c.clearResult(),"Idle result could not be cleared");check(!c.snapshot()->selection && !c.snapshot()->report,"Cleared result retained local choice");
            check(held->report->audio().frames==4,"GUI borrower lost cleared result");
            c.requestShutdown();wait([&]{return c.snapshot()->closed;});
        }
        check(held->report->audio().frames==4,"Borrowed result did not outlive facade");held.reset();retired(memory);
        auto refuses=[&](auto action,ErrorCode expected=ErrorCode::InvalidState) {
            bool refused=false;try {action();} catch (const ProjectError &e) {refused=e.code()==expected;}
            check(refused,"Invalid replacement context/result admitted");
        };
        Asset asset;asset.relativePath="media/missing.wav";asset.sha256=originalHash;
        asset.sampleRate=48000;asset.layout={LayoutKind::Mono,1};asset.frames=4;
        auto model=std::make_shared<Session>(makeOneTrackSession("Recovery","Track"));model->assets.push_back(asset);
        ControllerSnapshot project;project.session=model;project.root=root;project.projectEpoch=7;
        std::shared_ptr<const WaveCheckSnapshot> replacement;
        {
            WaveCheckOptions options;options.memory=memory;WaveCheckController c(options);
            check(c.submitAssetReplacement(file,project,asset.id,31)==Admission::Accepted,"Asset replacement refused");
            replacement=finish(c);
            check(replacement->phase==WaveCheckPhase::Complete && replacement->replacement &&
                replacement->replacement->context==AssetReplacementContext{7,0,31,replacement->job},
                "Replacement not bound to actual admitted job/revision zero");
            check(replacement->replacement->identity.relativePath.empty() &&
                replacement->replacement->identity.nativeEvidence.empty(),"Candidate inherited project path authority/evidence");
            verifyWaveAssetReplacement(*replacement,project,asset.id,31,memory);
            for (unsigned mode=0;mode<8;++mode) {
                auto current=project;auto result=*replacement;auto selected=asset.id;auto request=std::uint64_t(31);
                if(mode==0)++current.projectEpoch;
                if(mode==1)++current.modelRevision;
                if(mode==2)++request;
                if(mode==3)++result.job;
                if(mode==4)++result.childPid;
                if(mode==5)selected=Id::generate();
                if(mode==6)current.closing=true;
                if(mode==7)current.io=IoOperation::Open;
                refuses([&]{verifyWaveAssetReplacement(result,current,selected,request,memory);});
            }
            auto changed=std::make_shared<Session>(*model);changed->assets[0].frames++;
            auto current=project;current.session=changed;
            refuses([&]{verifyWaveAssetReplacement(*replacement,current,asset.id,31,memory);});
            ResourceLedger foreign(64*1024*1024);
            refuses([&]{verifyWaveAssetReplacement(*replacement,project,asset.id,31,foreign);});
            check(c.clearResult() && !c.snapshot()->replacement,"Clear retained replacement ticket");
            check(c.submit(root,name)==Admission::Accepted,"Ordinary media check refused after replacement");
            const auto ordinary=finish(c);
            check(!ordinary->replacement && ordinary->job==replacement->job+1,"Ordinary job inherited replacement identity");
            refuses([&]{verifyWaveAssetReplacement(*ordinary,project,asset.id,31,memory);});
        }
        verifyWaveAssetReplacement(*replacement,project,asset.id,31,memory);
        replacement.reset();retired(memory);
        {
            std::atomic<bool> reached=false,release=false;WaveCheckOptions options;options.memory=memory;
            options.beforeTerminalPublish=[&]{reached=true;while(!release)QThread::msleep(1);};
            WaveCheckController c(options);Release guard{release};
            check(c.submitAssetReplacement(file,project,asset.id,32)==Admission::Accepted,"Terminal cancellation fixture refused");
            wait([&]{return reached.load();});
            check(c.snapshot()->busy && c.snapshot()->childPid && !c.snapshot()->report,
                "Completed child escaped terminal-publication barrier");
            c.requestCancel();release=true;const auto result=finish(c);
            check(result->phase==WaveCheckPhase::Canceled && result->error==ErrorCode::Canceled && !result->report,
                "Cancellation before terminal publication qualified a replacement");
            refuses([&]{verifyWaveAssetReplacement(*result,project,asset.id,32,memory);});
        }
        retired(memory);
        for(unsigned mode=0;mode<4;++mode) {
            auto different=std::make_shared<Session>(*model);
            if(mode==0)different->assets[0].sha256[0]=originalHash[0]=='a' ? 'b' : 'a';
            if(mode==1)different->assets[0].frames++;
            if(mode==2)different->assets[0].sampleRate=44100;
            if(mode==3)different->assets[0].layout={LayoutKind::Stereo,2};
            auto current=project;current.session=different;
            WaveCheckOptions options;options.memory=memory;WaveCheckController c(options);
            check(c.submitAssetReplacement(file,current,asset.id,33)==Admission::Accepted,"Mismatch fixture not submitted");
            const auto result=finish(c);
            check(result->childPid && result->childExit==0 && result->phase==WaveCheckPhase::Fault &&
                result->error==ErrorCode::MediaMismatch && !result->report,"Wrong asset content qualified as replacement");
        }
        retired(memory);
        {
            std::atomic<bool> reached=false,release=false;WaveCheckOptions options;options.memory=memory;
            options.beforeSpawn=[&]{reached=true;while(!release)QThread::msleep(1);};
            WaveCheckController c(options);Release guard{release};
            check(c.submitAssetReplacement(file,project,asset.id,34)==Admission::Accepted,"Cancelable replacement refused");
            wait([&]{return reached.load();});const auto job=c.snapshot()->job;
            check(c.submitAssetReplacement(file,project,asset.id,35)==Admission::Full && c.snapshot()->job==job,
                "Full replacement changed admitted job");
            c.requestCancel();release=true;const auto result=finish(c);
            check(result->phase==WaveCheckPhase::Canceled && !result->report,"Canceled replacement qualified");
            refuses([&]{verifyWaveAssetReplacement(*result,project,asset.id,34,memory);});
        }
        retired(memory);
        {
            WaveCheckOptions options;options.memory=memory;WaveCheckController c(options);
            for (unsigned mode=0;mode<3;++mode) {
                auto invalid=std::make_shared<Session>(*model);
                if(mode==0)invalid->assets[0].sha256.resize(4096,'a');
                if(mode==1)invalid->assets[0].sha256[0]='A';
                if(mode==2)invalid->assets[0].frames=0;
                auto current=project;current.session=invalid;
                refuses([&]{c.submitAssetReplacement(file,current,asset.id,36);});
                check(!c.snapshot()->job && !c.snapshot()->busy,"Invalid identity consumed a job");retired(memory);
            }
        }
        {
            ResourceLedger small(16384);WaveCheckOptions options;options.memory=small;WaveCheckController c(options);
            refuses([&]{c.submitAssetReplacement(file,project,asset.id,36);},ErrorCode::ResourceLimit);
            check(!c.snapshot()->job && !c.snapshot()->busy,"Failed ticket admission consumed a job");retired(small);
            refuses([&]{c.submitAssetReplacement(file,project,asset.id,0);});
        }
        {
            std::atomic<bool> reached=false,release=false,offGui=false;WaveCheckOptions options;options.memory=memory;
            options.beforeSpawn=[&]{offGui=QThread::currentThread()!=app.thread();reached=true;while (!release) QThread::msleep(1);};
            WaveCheckController c(options);Release guard{release};check(c.submit(root,name)==Admission::Accepted,"Held job refused");wait([&]{return reached.load();});
            check(offGui && c.submit(root,name)==Admission::Full && !c.clearResult(),"Media single flight/off-GUI admission failed");
            check(memory.usage().reservedBytes>16*1024*1024,"Child work was not admitted before spawn");c.requestCancel();release=true;
            const auto s=finish(c);check(s->phase==WaveCheckPhase::Canceled && !s->childPid && !s->childExit,"Cancel before spawn launched a child");
        }
        retired(memory);
        {
            WaveCheckOptions options;options.memory=memory;WaveCheckController c(options);
            check(c.submit(root,"missing.wav")==Admission::Accepted,"Missing selection not admitted");const auto s=finish(c);
            check(s->phase==WaveCheckPhase::Fault && s->error==ErrorCode::MissingMedia && s->childPid && s->childExit!=0,"Missing media refusal lost");
        }
        retired(memory);
        for (unsigned mode=0;mode<14;++mode) {
            WaveCheckOptions options;options.memory=memory;std::atomic<bool> exercised=false;
            options.afterChild=[&,mode](std::string &raw,std::size_t pid) {
                auto j=nlohmann::json::parse(raw);
                switch (mode) {
                case 0:j["workerPid"]=pid+1;break;case 1:j["relative"]="other.wav";break;
                case 2:j["decodedFrames"]=0;break;case 3:j["sourceBytes"]=20;break;
                case 4:j["frames"]=-1;break;case 5:j["channels"]=1025;break;
                case 6:j["bitsPerSample"]=64;break;case 7:j["channelMask"]=1;break;
                case 8:j["peakLinear"]=-1;break;case 9:j["peakLinear"]=2;break;
                case 10:j["ioOperations"]=0;break;case 11:j["sourceSha256"]="BAD";break;
                case 12:j["unexpected"]="foreign";break;case 13:j["relative"]=nlohmann::json::array({name});break;
                }
                raw=j.dump();exercised=true;
            };
            WaveCheckController c(options);check(c.submit(root,name)==Admission::Accepted,"Protocol-corruption request refused");const auto s=finish(c);
            check(exercised && s->childPid && s->childExit==0,"Corruption did not run after a successful actual child");
            check(s->phase==WaveCheckPhase::Fault && s->error==ErrorCode::InvalidState && !s->report,"Corrupt report admitted");
        }
        retired(memory);
        for (unsigned mode=0;mode<3;++mode) {
            WaveCheckOptions options;options.memory=memory;
            options.afterChild=[mode](std::string &raw,std::size_t) {
                if (!mode) raw.insert(raw.find('{')+1,"\"complete\":true,");
                else if (mode==1) raw="["+raw+"]";
                else {const auto end=raw.find_last_not_of(" \r\n\t");raw.erase(end,1);}
            };
            WaveCheckController c(options);c.submit(root,name);const auto s=finish(c);
            check(s->phase==WaveCheckPhase::Fault && s->error==ErrorCode::InvalidState && s->childPid && s->childExit==0,"Duplicate/deep/malformed report admitted");
        }
        retired(memory);
        const auto helper=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
            QStringLiteral("/sc-import-lifecycle-probe.exe");
#else
            QStringLiteral("/sc-import-lifecycle-probe");
#endif
        {
            WaveCheckOptions options;options.memory=memory;options.program=helper;options.maximumSourceBytes=444444;
            WaveCheckController c(options);check(c.submit(root,name)==Admission::Accepted,"CRLF diagnostic fixture refused");const auto s=finish(c);
            check(s->phase==WaveCheckPhase::Fault && s->error==ErrorCode::MissingMedia && s->childPid && s->childExit==1,
                  "Actual CRLF diagnostic lost its stable error code");
        }
        retired(memory);
        for (unsigned scenario=0;scenario<3;++scenario) {
            WaveCheckOptions options;options.memory=memory;options.program=helper;options.deadlineMilliseconds=scenario==0 ? 100 : 2000;
            if (scenario==2) options.maximumSourceBytes=555555;
            WaveCheckController c(options);c.submit(root,name);
            if (scenario==1) {wait([&]{return c.snapshot()->childPid!=0;});c.requestCancel();}
            const auto s=finish(c);check(s->childPid && s->childExit.has_value(),"Live child was not retained to terminal exit");
            if (!scenario) check(s->timedOut && s->phase==WaveCheckPhase::Fault,"Deadline did not stop live child");
            if (scenario==1) check(s->phase==WaveCheckPhase::Canceled,"Live child cancel failed");
            if (scenario==2) check(s->error==ErrorCode::ResourceLimit && s->phase==WaveCheckPhase::Fault,"Flood output admitted");
            check(memory.usage().reservedBytes==s->selection->lease.bytes(),"Terminal child work did not retire");
        }
        retired(memory);
#ifndef _WIN32
        const auto native=root/"native?name.wav";{std::ofstream out(native,std::ios::binary);out<<original;}
        {WaveCheckOptions options;options.memory=memory;WaveCheckController c(options);c.submitReplacement(native);const auto s=finish(c);
         check(s->phase==WaveCheckPhase::Complete && s->selection->selectedFilename && s->report->relative()=="native?name.wav","Explicit native filename mapping refused");}
        retired(memory);
#endif
        ResourceLedger tiny(16384);{WaveCheckOptions options;options.memory=tiny;WaveCheckController c(options);c.submit(root,name);const auto s=finish(c);
        check(s->phase==WaveCheckPhase::Fault && s->error==ErrorCode::ResourceLimit && !s->childPid,"Insufficient work credit spawned a child");}
        retired(tiny);
        {std::ifstream in(file,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});check(bytes==original,"Source audio changed");}
        std::cout<<"PASS: "<<checks<<" actual media-child/controller checks; PID/report, quotas, Unicode, cancel/deadline/flood and retirement; no audio device\n";return 0;
    } catch (const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
