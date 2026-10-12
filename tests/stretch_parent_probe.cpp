// SPDX-License-Identifier: GPL-3.0-only
// Qualification process, not the installed GUI. Uses the production controller.
#include "stretch_controller.hpp"
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "media_io.hpp"
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
using Json=nlohmann::json;
namespace {
Json observationTime(){
    return {{"probeSteadyNs",std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()},
            {"probeUtcNs",std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}};
}
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void put(std::ofstream &out,std::uint64_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)out.put(char((value>>(8*i))&255));}
void receipt(const std::filesystem::path &path,const Json &value){
    auto temporary=path;temporary+=".partial";
    std::ofstream out(temporary,std::ios::binary);out<<value.dump()<<'\n';out.close();
    require(bool(out),"Cannot write qualification receipt");std::filesystem::rename(temporary,path);
}
void fixture(const std::filesystem::path &root){
    require(std::filesystem::create_directory(root),"Fixture already exists");std::filesystem::create_directory(root/"media");
    constexpr unsigned frames=1048576;std::ofstream out(root/"media"/utf8Path("raw-été.wav"),std::ios::binary);
    out<<"RIFF";put(out,36+frames*8ULL,4);out<<"WAVEfmt ";put(out,16,4);put(out,3,2);put(out,2,2);put(out,48000,4);put(out,48000*8,4);put(out,8,2);put(out,32,2);out<<"data";put(out,frames*8ULL,4);
    for(unsigned f=0;f<frames;++f)for(unsigned ch=0;ch<2;++ch)put(out,std::bit_cast<std::uint32_t>(float(int((f+ch*17)%1024)-512)/256.f),4);
    out.close();require(bool(out),"Cannot write source fixture");
    auto s=makeOneTrackSession("Owned abrupt parent probe","Audio");Asset a;a.relativePath="media/raw-été.wav";a.frames=frames;a.layout={LayoutKind::Stereo,2};a.sha256=hashMediaFile(root/utf8Path(a.relativePath));s.assets={a};s.tracks[0]=makeAudioTrack("Audio",a.layout,s.sampleRate);
    Clip clip;clip.assetId=a.id;clip.lengthFrames=frames;s.tracks[0].clips={clip};validate(s);ProjectStore(root).save(s);
}
StretchOptions options(QString helper){StretchOptions o;o.program=std::move(helper);o.policy.deadlineMilliseconds=5000;o.policy.maximumInputFrames=2000000;o.policy.maximumSourceBytes=16*1024*1024;o.policy.maximumOutputBytes=16*1024*1024;return o;}
}
int main(int argc,char **argv){const auto mainEntry=observationTime();QCoreApplication app(argc,argv);try{
    const auto args=app.arguments();require(args.size()>=3,"Missing probe arguments");
    const auto root=utf8Path(args[2].toUtf8().toStdString());
    if(args[1]=="--init"){fixture(root);return 0;}
    auto o=options(args.value(5));auto source=std::make_shared<ControllerSnapshot>();source->root=root;source->session=SessionSnapshots(o.memory).copy(ProjectStore(root).load());source->projectEpoch=1;const auto sourceReady=observationTime();
    if(args[1]=="--scan"){
        require(args.size()==3,"Unexpected scan arguments");const auto controllerBegin=observationTime();StretchController c(o);const auto controllerReady=observationTime();require(c.scan(source)==Admission::Accepted,"Scan refused");const auto scanSubmitted=observationTime();
        while(c.snapshot()->busy)QThread::msleep(1);const auto scanComplete=observationTime();const auto s=c.snapshot();require(s->inventory && s->inventory->size()==1 && !s->result,"Scan lost or attached job");
        const auto &entry=s->inventory->at(0);static const char *names[]={"Ready","Attached","Stale","Incomplete","Active","Invalid"};
        const auto hashBegin=observationTime();const auto before=hashMediaFile(root/"project.json");const auto hashComplete=observationTime();bool reviewed=false;
        if(entry.status==StretchRecoveryStatus::Ready){require(c.review(source,entry.operation)==Admission::Accepted,"Review refused");while(c.snapshot()->busy)QThread::msleep(1);require(c.snapshot()->result && c.snapshot()->recovered && !c.snapshot()->childPid,"Review failed or launched worker");reviewed=true;}
        require(hashMediaFile(root/"project.json")==before,"Recovery changed saved project");
        std::cout<<Json({{"status",names[unsigned(entry.status)]},{"operation",entry.operation.str()},{"reviewed",reviewed},{"attached",false},{"projectSha256",before},{"timing",{{"steadyDomain","cpp-probe-steady-clock-ns"},{"utcDomain","cpp-probe-unix-utc-ns"},{"mainEntry",mainEntry},{"sourceReady",sourceReady},{"controllerBegin",controllerBegin},{"controllerReady",controllerReady},{"scanSubmitted",scanSubmitted},{"scanComplete",scanComplete},{"projectHashBegin",hashBegin},{"projectHashComplete",hashComplete},{"reportReady",observationTime()}}}}).dump()<<'\n';return 0;
    }
    require(args[1]=="--render" && args.size()==6,"Unexpected render arguments");const auto stage=args[3];const auto ticket=utf8Path(args[4].toUtf8().toStdString());auto release=ticket;release+=".release";
    StretchController *owner=nullptr;
    auto hold=[&](const char *point){const auto s=owner->snapshot();receipt(ticket,{{"pid",QCoreApplication::applicationPid()},{"helperPid",s->childPid},{"operation",s->selection->operation.str()},{"point",point},{"attached",false},{"childExit",s->childExit ? Json(*s->childExit) : Json(nullptr)},{"timedOut",s->timedOut},{"abnormalExit",s->abnormalExit},{"error",s->error ? Json(unsigned(*s->error)) : Json(nullptr)},{"deadlineMilliseconds",o.policy.deadlineMilliseconds},{"timing",{{"steadyDomain","cpp-probe-steady-clock-ns"},{"utcDomain","cpp-probe-unix-utc-ns"},{"mainEntry",mainEntry},{"sourceReady",sourceReady},{"holdEntry",observationTime()}}}});while(!std::filesystem::exists(release))QThread::msleep(1);};
    if(stage!="after-complete")o.afterReady=[&]{hold("before-start-acknowledgement");};
    if(stage=="after-complete")o.afterChild=[&]{hold("after-child-before-verification");};
    StretchController c(o);owner=&c;require(c.render(source,source->session->tracks[0].id,source->session->tracks[0].clips[0].id,{1,1,0,false})==Admission::Accepted,"Render refused");
    while(c.snapshot()->busy)QThread::msleep(1);const auto s=c.snapshot();require(s->phase==StretchPhase::Complete,"Unexpected render failure");return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
