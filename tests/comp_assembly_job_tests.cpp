// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_assembly_job.hpp>
#include <soundcurrent/comp_media_review.hpp>
#include <soundcurrent/comp_playback_source.hpp>
#include <soundcurrent/playback_reader.hpp>
#include <soundcurrent/export.hpp>
#include "media_io.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid assembly publication accepted");}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);check(bool(f),"Missing job evidence");return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view v){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(v.data(),std::streamsize(v.size()));check(bool(f),"Cannot save job evidence");}
std::string hash(std::string_view bytes){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});return h.digest();}
SNDFILE *open(const std::filesystem::path &p,int mode,SF_INFO &info){
#ifdef _WIN32
    return sf_wchar_open(p.c_str(),mode,&info);
#else
    return sf_open(p.c_str(),mode,&info);
#endif
}
std::vector<float> decode(const std::filesystem::path &p){SF_INFO info{};auto f=open(p,SFM_READ,info);check(f && info.channels==2 && info.frames>0,"Invalid job wave");std::vector<float> values(std::size_t(info.frames)*2);const auto n=sf_readf_float(f,values.data(),info.frames);const auto close=sf_close(f);check(n==info.frames && close==0,"Job wave read failed");return values;}
void wave(const std::filesystem::path &p,const Asset &a,const std::vector<float> &v){SF_INFO info{};info.channels=2;info.samplerate=int(a.sampleRate);info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;auto f=open(p,SFM_WRITE,info);check(f!=nullptr,"Cannot rewrite fixture wave");const auto n=sf_writef_float(f,v.data(),a.frames);const auto close=sf_close(f);check(n==a.frames && close==0,"Fixture wave write failed");}
Json adoptionWorkflow(const std::filesystem::path &root,const Session &registered,const CompHandlePlanContext &context,
    const CompPairSelection &pair,const CompAssemblyReference &reference,const VerifiedCompAssembly &verified,ResourceLedger ledger){
    auto s=registered;const auto &plan=pair.recipe().plan();
    const auto identity=[](const CompHandleEnvelopeData &h){return CompAssemblySourceIdentity{h.expected,h.retained,h.raw,h.base,h.retainedAsset,h.nominalAsset};};
    const auto selection=std::find_if(s.compPairSelections.begin(),s.compPairSelections.end(),[&](const auto &p){return p.operation==reference.selectionOperation;});
    check(selection!=s.compPairSelections.end(),"Actual assembly has no independent selection");
    ApplyCompAssembly edit{plan.handles().transition(),reference,*selection,
        {identity(plan.handles().outgoing().data()),identity(plan.handles().incoming().data())},{}};
    check(verified.requestDigest()==reference.requestSha256,"Adoption lacks independent request binding");
    for(unsigned side=0;side<2;++side)if(verified.outputs()[side])
        edit.sources[side]=prepareCompPlaybackSource(s,context,plan,side,verified.outputs()[side],ledger).data();
    EditHistory history(s);check(history.structural({edit}),"Actual assembly adoption failed");
    const auto adopted=s;check(history.undo() && s==registered,"Actual assembly Undo lost original state");
    check(history.redo() && s==adopted,"Actual assembly Redo lost source provenance");
    ProjectStore(root).save(s);auto reopened=ProjectStore(root).load();check(reopened==adopted,"Actual assembly did not reopen");
    write(root/"adopted-project.json",read(root/"project.json"));
    const auto &track=reopened.tracks.at(0);check(track.layout.channels==2,"Playback fixture needs stereo");
    const auto first=track.clips.front().startFrame;
    Frame end=first;for(const auto &c:track.clips)end=std::max(end,c.startFrame+c.lengthFrames);
    PlaybackConfig config;config.sampleRate=reopened.sampleRate;config.layout=track.layout;
    config.startFrame=first;config.endFrame=end;config.maximumCallbackFrames=193;config.slabFrames=512;
    std::vector<float> live;live.reserve(std::size_t(end-first)*2);
    {
        PlaybackPipe pipe(config);TrackReader reader(pipe,root,reopened,track.id);
        PlaybackProcessor processor(reopened,track.id,pipe);
        std::array<float,193> left{},right{};std::array<float *,2> views{left.data(),right.data()};
        for(Frame at=first;at<end;){
            while(reader.fillOne()){}
            const auto count=std::uint32_t(std::min<Frame>(193,end-at));
            const auto report=processor.process(views,count);
            check((report.status==PlaybackStatus::Running || report.status==PlaybackStatus::Complete) &&
                report.timelineFrames==count && !report.missingFrames,"Reopened live core playback failed");
            for(unsigned i=0;i<count;++i){live.push_back(left[i]);live.push_back(right[i]);}at+=count;
        }
        check(!pipe.missingFrames() && !reader.sanitizedSamples(),"Comp playback lost or sanitized samples");
    }
    std::filesystem::create_directory(root/"exports");
    ExportSpec spec(track.id);spec.startFrame=first;spec.endFrame=end;spec.blockFrames=127;
    const auto exported=exportTrackWav(root,reopened,root/"exports/assembled.wav",spec);
    const auto output=decode(exported.destination);check(output==live,"Live core and offline export differ after assembly reopen");
    check(exported.sampleRate==reopened.sampleRate && exported.frames==end-first && exported.peak>1,
        "Assembly export changed rate/extent or floating headroom");
    check(ProjectStore(root).load()==adopted,"Playback/export mutated adopted project");
    check(history.undo() && s==registered,"Post-playback Undo lost original project");
    ProjectStore(root).save(s);check(ProjectStore(root).load()==registered,"Pending reference was not restored after Undo");
    return {{"schema","1.23"},{"savedReopened",true},{"undoRedo",true},{"liveCoreSamples",live.size()},
        {"liveCallbackFrames",193},{"exportBlockFrames",127},{"liveEqualsExport",true},{"exportPeak",exported.peak},
        {"exportFileSha256",exported.fileSha256},{"exportSampleSha256",exported.sampleSha256},
        {"physicalAudio",false},{"mainWindow",false},{"registeredProjectRestored",true}};
}
}
int main(int argc,char **argv){try{
    if(argc==3 && std::string_view(argv[1])=="--image-probe"){
        std::cout<<compAssemblyExecutableSha256()<<std::endl;const auto ack=utf8Path(argv[2]);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!std::filesystem::exists(ack) && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(std::filesystem::exists(ack),"Missing image probe acknowledgment");std::cout<<compAssemblyExecutableSha256()<<std::endl;return 0;}

    check(argc==1,"Invalid publication fixture arguments");
    Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(512*1024*1024,"Assembly publication test");const auto s=ProjectStore(root).load();const CompHandlePlanContext context{51,"owned-assembly-owner"};auto pair=loadReferencedCompPairSelection(root,s,context,s.compPairSelections.at(0).operation,ledger);const auto executable=compAssemblyExecutableSha256();Json jobs=Json::array();
    auto request=[&]{std::array<std::optional<Id>,2> ids{Id::generate(),Id::generate()};return prepareCompAssemblyRequest(s,context,pair,Id::generate(),ids,executable,{},ledger);};
    auto r=request();auto reopened=decodeCompAssemblyRequest(r.bytes(),r.digest(),s,context,pair,ledger);check(reopened.bytes()==r.bytes(),"Request changed across codec");
    refuses([&]{decodeCompAssemblyRequest(r.bytes(),std::string(64,'0'),s,context,pair,ledger);});
    for(unsigned mode=0;mode<5;++mode){auto j=Json::parse(r.bytes());if(mode==0)j["extra"]=true;if(mode==1)j["policy"]["maximumFileBytes"]=32;if(mode==2)j["outputs"][1]=j["outputs"][0];if(mode==3)j["policy"]["deadlineMilliseconds"]=1.0;if(mode==4)j["selectionDigest"]=std::string(64,'0');const auto bad=j.dump();refuses([&]{decodeCompAssemblyRequest(bad,hash(bad),s,context,pair,ledger);});}
    const auto duplicate=std::string("{\"protocol\":\"sc-comp-assembly-v1\",")+std::string(r.bytes()).substr(1);refuses([&]{decodeCompAssemblyRequest(duplicate,hash(duplicate),s,context,pair,ledger);});
    const auto folder=[&](const CompAssemblyRequest &v){return root/"media/comp-assemblies"/v.operation().str();};
    const auto record=[&](const CompAssemblyRequest &v,std::string_view label){write(root/(v.operation().str()+".request.json"),v.bytes());jobs.push_back({{"case",label},{"operation",v.operation().str()},{"requestDigest",v.digest()}});};
    auto registered=s;const auto reference=persistCompAssemblyRequest(root,s,context,pair,r,ledger);
    check(persistCompAssemblyRequest(root,s,context,pair,r,ledger)==reference,"Idempotent request persistence changed reference");
    EditHistory history(registered);check(history.structural({AddCompAssembly{reference}}),"Trusted assembly registration failed");
    ProjectStore(root).save(registered);check(ProjectStore(root).load()==registered,"Assembly reference did not reopen");
    auto loaded=loadReferencedCompAssemblyRequest(root,registered,context,pair,r.operation(),ledger);
    check(loaded.bytes()==r.bytes(),"Saved request bytes changed");
    check(history.undo() && registered==s,"Reference Undo changed source project");
    refuses([&]{loadReferencedCompAssemblyRequest(root,registered,context,pair,r.operation(),ledger);});
    check(history.redo(),"Reference Redo failed");
    auto badReference=registered;badReference.compAssemblies[0].requestSha256=std::string(64,'0');
    refuses([&]{loadReferencedCompAssemblyRequest(root,badReference,context,pair,r.operation(),ledger);});
    badReference=registered;badReference.compAssemblies[0].outputs[0]=Id::generate();
    refuses([&]{loadReferencedCompAssemblyRequest(root,badReference,context,pair,r.operation(),ledger);});
    const auto sidecar=root/"media/comp-assembly-requests"/(r.operation().str()+".json");
    write(sidecar,std::string(r.bytes())+" ");refuses([&]{loadReferencedCompAssemblyRequest(root,registered,context,pair,r.operation(),ledger);});
    refuses([&]{persistCompAssemblyRequest(root,registered,context,pair,r,ledger);});write(sidecar,r.bytes());
    auto missingSidecar=sidecar;missingSidecar+=".missing";
    std::filesystem::rename(sidecar,missingSidecar);
    check(ProjectStore(root).load()==registered,"Missing request prevented project reopen");
    refuses([&]{loadReferencedCompAssemblyRequest(root,registered,context,pair,r.operation(),ledger);});
    std::filesystem::rename(missingSidecar,sidecar);
    for(unsigned slot=0;slot<2;++slot){auto bad=prepareCompAssemblyRequest(s,context,pair,slot?Id::generate():s.tracks[0].id,
        {slot?s.tracks[0].id:Id::generate(),Id::generate()},executable,{},ledger);
        refuses([&]{persistCompAssemblyRequest(root,s,context,pair,bad,ledger);});
        check(!std::filesystem::exists(root/"media/comp-assembly-requests"/(bad.operation().str()+".json")),"Colliding request sidecar published");}
    record(r,"complete");check(inspectCompAssembly(root,s,context,pair,r,ledger).state==CompAssemblyRecoveryState::Absent,"Fresh job not absent");unsigned observed=0;
    publishCompAssembly(root,s,context,pair,r,ledger,[&](auto,unsigned){check(inspectCompAssembly(root,s,context,pair,r,ledger).state==CompAssemblyRecoveryState::Active,"Live writer not active");refuses([&]{verifyCompAssembly(root,s,context,pair,r,ledger);});++observed;});check(observed==5,"Publication stages missing");
    auto result=verifyCompAssembly(root,s,context,pair,r,ledger);check(result.comparedSamples()>0 && result.requestDigest()==r.digest(),"Verified pair lost request binding");const auto outputs=result.outputs();const auto originalComplete=read(folder(r)/"complete.json");
    const auto adoption=adoptionWorkflow(root,registered,context,pair,reference,result,ledger);
    refuses([&]{publishCompAssembly(root,s,context,pair,r,ledger);});check(read(folder(r)/"complete.json")==originalComplete,"Retry replaced completed job");
    write(folder(r)/"cancel.request","cancel\n");{auto recovered=inspectCompAssembly(root,s,context,pair,r,ledger);check(recovered.state==CompAssemblyRecoveryState::Complete && recovered.cancellationRequested && recovered.result.has_value(),"Late cancel hid committed assembly");}
    // Counterfeit internally consistent hashes: scope-only review passes because
    // this change lies inside the permitted blend, but complete recomputation fails.
    auto altered=outputs;const auto audio=root/utf8Path(outputs[0]->relativePath);const auto originalAudio=read(audio);auto samples=decode(audio);const auto &pcm=pair.recipe().plan().outgoing().pcm;
    samples[std::size_t(pcm.prefixFrames+pcm.currentFrames-1)*2]+=.125f;wave(audio,*outputs[0],samples);altered[0]->sha256=hashMediaFile(audio);auto forged=Json::parse(originalComplete);forged["outputs"][0]["sha256"]=altered[0]->sha256;media_io::SampleHash sampleHash;sampleHash.update(samples);forged["outputs"][0]["sampleSha256"]=sampleHash.digest();double peak=0;for(auto v:samples)peak=std::max(peak,std::abs(double(v)));forged["outputs"][0]["peakLinear"]=peak;write(folder(r)/"complete.json",forged.dump());
    {auto scope=reviewCompMedia(root,s,context,pair.recipe(),altered,ledger);check(scope.outputs()==altered,"Permitted-blend corruption did not isolate full-PCM gate");}
    refuses([&]{verifyCompAssembly(root,s,context,pair,r,ledger);});check(inspectCompAssembly(root,s,context,pair,r,ledger).state==CompAssemblyRecoveryState::Invalid,"Counterfeit output appeared complete");write(audio,originalAudio);write(folder(r)/"complete.json",originalComplete);
    write(folder(r)/"intent.json",std::string(r.bytes())+" ");check(inspectCompAssembly(root,s,context,pair,r,ledger).state==CompAssemblyRecoveryState::Invalid,"Changed original intent accepted");write(folder(r)/"intent.json",r.bytes());
    auto stale=context;++stale.projectEpoch;refuses([&]{verifyCompAssembly(root,s,stale,pair,r,ledger);});
    for(unsigned scenario=0;scenario<3;++scenario){auto interrupted=request();record(interrupted,scenario==0?"after-close":scenario==1?"cancel-after-first-file":"before-completion");bool reached=false;
        refuses([&]{publishCompAssembly(root,s,context,pair,interrupted,ledger,[&](auto stage,unsigned side){
            if((scenario==0 && stage==CompAssemblyStage::FileClosed && side==0)||(scenario==2 && stage==CompAssemblyStage::BeforeCompletion)){reached=true;throw ProjectError(ErrorCode::Io,"Injected owned publication interruption");}
            if(scenario==1 && stage==CompAssemblyStage::FilePublished && side==0){reached=true;write(folder(interrupted)/"cancel.request","cancel\n");}
        });});check(reached && !std::filesystem::exists(folder(interrupted)/"complete.json"),"Interrupted job committed");auto recovered=inspectCompAssembly(root,s,context,pair,interrupted,ledger);check(recovered.state==(scenario==1?CompAssemblyRecoveryState::Canceled:CompAssemblyRecoveryState::Interrupted) && !recovered.result,"Interrupted job yielded accepted pair");
        refuses([&]{publishCompAssembly(root,s,context,pair,interrupted,ledger);});
    }
    auto pre=request();record(pre,"precancel");std::stop_source canceled;canceled.request_stop();refuses([&]{publishCompAssembly(root,s,context,pair,pre,ledger,{},canceled.get_token());});check(!std::filesystem::exists(folder(pre)),"Precanceled job created files");
    auto tinyRequest=request();ResourceLedger tiny(1,"Refused job quota");refuses([&]{publishCompAssembly(root,s,context,pair,tinyRequest,tiny);});check(!std::filesystem::exists(folder(tinyRequest)) && !tiny.usage().reservedBytes,"Quota refusal modified job or leaked credit");
    auto wrong=prepareCompAssemblyRequest(s,context,pair,Id::generate(),{Id::generate(),Id::generate()},std::string(64,'0'),{},ledger);refuses([&]{publishCompAssembly(root,s,context,pair,wrong,ledger);});check(!std::filesystem::exists(folder(wrong)),"Wrong executable created job");
    {auto checked=verifyCompAssembly(root,s,context,pair,r,ledger);check(checked.outputs()==outputs,"Restored pair changed verified identity");}
    check(ProjectStore(root).load()==registered,"Assembly publication mutated registered project");
    const auto relocated=root.parent_path()/"relocated-assembly";std::filesystem::copy(root,relocated,std::filesystem::copy_options::recursive);
    const auto moved=ProjectStore(relocated).load();const CompHandlePlanContext movedContext{61,"relocated-assembly-owner"};
    auto movedPair=loadReferencedCompPairSelection(relocated,moved,movedContext,reference.selectionOperation,ledger);
    auto movedRequest=loadReferencedCompAssemblyRequest(relocated,moved,movedContext,movedPair,reference.operation,ledger);
    auto recovered=inspectCompAssembly(relocated,moved,movedContext,movedPair,movedRequest,ledger);
    check(moved==registered && recovered.state==CompAssemblyRecoveryState::Complete && recovered.result->outputs()==outputs,
        "Relocated trusted request did not recover exact completed output");
    Json report{{"checks",checks},{"executorSha256",executable},{"jobs",jobs},{"comparedSamples",result.comparedSamples()},{"completedJobs",1},{"interruptedJobs",2},{"canceledJobs",1},{"sourceBasedFullPcmRefusal",true},{"requestReferenceInProject",true},{"requestSidecarReopenRelocation",true},{"modelAdoptionChanged",true},{"adoption",adoption},{"qualityAdmission",false},{"osMemoryCeilingClaimed",false},{"actualPowerLossTested",false}};write(root/"assembly-job-report.json",report.dump(2));std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
