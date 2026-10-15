// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_assembly_adoption.hpp>
#include <soundcurrent/comp_transition_preparation.hpp>
#include <soundcurrent/comp_regeneration_history.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/playback_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/media_cache.hpp>
#include "windows_file_path.hpp"
#include <bit>
#include <sndfile.h>
#include "media_io.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid repeated assembly accepted");}
std::string read(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);check(bool(f),"Missing repeat fixture file");return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view s){std::ofstream f(fileSystemApiPath(p),std::ios::binary|std::ios::trunc);f.write(s.data(),std::streamsize(s.size()));check(bool(f),"Cannot save repeated fixture");}
const CompHandlePlanContext context{141,"repeated-pair/é-日本"};
CompSpanWorkerPolicy policy(){return {256*1024*1024,64*1024*1024,64*1024*1024,10000};}
std::vector<float> playback(const std::filesystem::path &root,const Session &s,Frame first,Frame end){
    const auto &t=s.tracks[0];PlaybackConfig config;config.sampleRate=s.sampleRate;config.layout=t.layout;
    config.startFrame=first;config.endFrame=end;config.maximumCallbackFrames=193;config.slabFrames=512;
    PlaybackPipe pipe(config);TrackReader reader(pipe,root,s,t.id);PlaybackProcessor processor(s,t.id,pipe);
    std::vector<float> result;std::array<float,193> left{},right{};std::array<float *,2> views{left.data(),right.data()};
    for(auto at=first;at<end;){while(reader.fillOne()){}const auto n=std::uint32_t(std::min<Frame>(193,end-at));auto r=processor.process(views,n);
        check((r.status==PlaybackStatus::Running || r.status==PlaybackStatus::Complete) && r.timelineFrames==n && !r.missingFrames,"Repeated live core playback failed");
        for(unsigned i=0;i<n;++i){result.push_back(left[i]);result.push_back(right[i]);}at+=n;}
    check(!pipe.missingFrames() && !reader.sanitizedSamples(),"Repeated playback sanitized or lost samples");return result;
}
}
int main(int argc,char **argv){try{
    check(argc==2,"Expected repeated fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());
    const auto mode=std::string_view(argv[1]);const bool protectedMode=input.value("protected",false),clocked=input.value("clocked",false);check(!clocked || protectedMode,"Clocked fixture requires semantic protection");ResourceLedger ledger(512*1024*1024,"Repeated comp production");Json result;
    {
        if(mode=="prepare"){
            auto s=input.at("generation")==0?decodeProject(read(root/"adopted-project.json")):ProjectStore(root).load();
            const bool mixed=input.value("mixed",false);auto &t=s.tracks[0];auto &out=t.clips[0];auto &in=t.clips[1];
            if(clocked && !mixed){
                const bool first=input.at("generation")==0;
                for(auto *c:{&out,&in}){
                    const auto asset=std::find_if(s.assets.begin(),s.assets.end(),[&](const auto &a){return a.id==c->assetId;});
                    check(asset!=s.assets.end(),"Clocked fixture asset missing");
                    const auto original=resolveCompRegenerationSource(s,t.id,c->id,context,ledger);
                    const bool uniform=!original.data().originalNominalClip.stretch->warp;
                    const Frame length=uniform?(first?16384:8000):(first?4000:3000);
                    const Frame sourceSpan=first?length*3/2:(length*2+2)/3;
                    c->sourceFrame=c==&out?asset->frames-sourceSpan:0;
                    if(first)c->sourceTiming={1,7};else c->sourceTiming={2,11};
                    c->lengthFrames=length;if(first)c->playbackRate={3,2};else c->playbackRate={2,3};
                    if(first)c->processing.gainDb=36;
                }
            }
            if(mixed){
                // Preserve a real assembled take, use a cropped active copy with
                // existing handles, and regenerate only the other original side.
                auto retained=out;retained.id=Id::generate();retained.takeOrigin.reset();
                TakeLane lane;lane.name="Retained assembled take";lane.captureStartFrame=retained.startFrame;
                lane.captureEndFrame=retained.startFrame+retained.lengthFrames;lane.clips.push_back(retained);
                out.takeOrigin=ClipTakeOrigin{t.id,lane.id,retained.id};t.takeLanes.push_back(std::move(lane));
                const Frame crop=protectedMode && !clocked?8192:64;out.sourceFrame+=crop;out.lengthFrames-=2*crop;
                const auto source=resolveCompRegenerationSource(s,t.id,in.id,context,ledger);
                in=source.data().originalNominalClip;in.sourceFrame=0;in.sourceTiming={};in.lengthFrames=protectedMode?source.data().originalNominal->frames:1000;
            }
            in.startFrame=out.startFrame+out.lengthFrames;validate(s);const auto boundary=in.startFrame;
            const auto original=resolveCompRegenerationSource(s,t.id,out.id,context,ledger);
            const Frame before=protectedMode && !clocked?3008:32;
            const Frame after=protectedMode && !clocked?(original.data().originalNominalClip.stretch->warp->markers.size()==1?4544:1472):32;
            auto plan=prepareCompProcessing(s,{t.id,out.id,in.id,boundary-before,boundary+after,ClipFadeCurve::EqualPower},context,8,ledger);
            check(plan.handles().outgoing().data().regeneration!=nullptr,"Outgoing history absent");
            if(clocked && !mixed)check(plan.outgoing().domain==CompProcessingDomain::ProjectAfterSrc && plan.incoming().domain==CompProcessingDomain::ProjectAfterSrc,"Clocked fixture did not exercise post-SRC pair assembly");
            if(mixed){
                const auto &h=plan.handles().outgoing().data();
                check(plan.outgoing().domain==CompProcessingDomain::ExistingMedia && plan.incoming().domain!=CompProcessingDomain::ExistingMedia,"Mixed source was not unchanged");
                check(h.retainedAsset.id==out.assetId && h.raw.id!=h.retainedAsset.id && !h.retained.stretch,"Retained assembled map was not exercised");
                try{prepareCompHandleEnvelope(s,t.id,out.id,out.startFrame,out.startFrame+out.lengthFrames+(protectedMode?16384:512),context,ledger);
                    throw std::runtime_error("Retained assembled coverage was exceeded");
                }catch(const ProjectError &e){check(std::string_view(e.what())=="Requested handles exceed retained take","Wrong retained assembled coverage refusal");}
            }
            if(clocked){
                const auto originalSession=s;const auto originalProject=read(root/"project.json");
                auto prepared=prepareOwnedCompTransition(root,s,context,plan.handles().transition(),8,input.at("workerSha256").get<std::string>(),policy(),{},ledger);
                check(prepared.selection().spanPolicy()==CompPairSpanPolicy::ClockedProtected && prepared.ownedBy(ledger),"Application preparation did not choose owned clocked policy");
                check(s==originalSession && read(root/"project.json")==originalProject,"Application preparation changed canonical project");
            }
            const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",compAssemblyExecutableSha256()};
            auto recipe=encodeCompProcessingRecipe(s,context,plan,Id::generate(),executor,ledger);
            auto pair=prepareCompPairSelection(std::move(recipe),executor,input.at("workerSha256").get<std::string>(),{Id::generate(),Id::generate()},policy(),ledger,clocked?CompPairSpanPolicy::ClockedProtected:protectedMode?CompPairSpanPolicy::ProtectedOriginal:CompPairSpanPolicy::UniformOnly);
            if(clocked)check(Json::parse(pair.manifest()).at("format")=="sc-comp-pair-selection-clocked-protected-v1","Clocked pair format not versioned");
            const auto pairRef=referenceCompPairSelection(s,context,pair);persistCompPairSelection(root,s,context,pair,ledger);
            applySessionEdits(s,{AddCompPairSelection{pairRef}});std::array<std::optional<Id>,2> outputs;
            Json requests=Json::array();for(unsigned side=0;side<2;++side)if(pair.children()[side]){outputs[side]=Id::generate();const auto &r=*pair.children()[side];
                if(clocked){
                    const auto &e=(side?pair.recipe().plan().handles().incoming():pair.recipe().plan().handles().outgoing()).data();
                    const auto &original=e.regeneration?e.regeneration->data().originalNominalClip:e.expected;
                    const bool protectedSource=original.stretch && original.stretch->warp;
                    check(r.isClockedProtected()==protectedSource && r.protocol()==(protectedSource?compClockedProtectedSpanProtocol:compRegenerationSpanProtocol) && r.requiresHistory(),"Clocked pair identity lost");
                    const auto expected=input.at("generation")==1?SourcePosition{1,1,2}:SourcePosition{1,0,1};
                    check((e.regeneration?e.regeneration->data().bakedPitchScale:SourcePosition{1,0,1})==expected,"Pair lost independently expected baked pitch");
                    if(protectedSource){
                        const auto &g=r.clockedProtectedSpan().geometry();
                        check(r.clockedProtectedSpan().bakedPitchScale()==expected && g.spans().size()==2,"Clocked pair lost pitch/original anchors");
                        if(input.at("generation")==1){unsigned outside=0;for(const auto &span:g.spans()){
                            const auto raw=addSourcePosition(g.map().exactRegion().rawOrigin,span.sourceAnchor);
                            if(sourcePositionLess(raw,e.map.front().raw) || !sourcePositionLess(raw,e.map.back().raw))++outside;
                        }check(outside==2,"Cropped post-SRC ancestor did not retain both outside anchors");}
                    }
                }
                else if(protectedMode){check(r.isProtected() && r.requiresHistory() && r.protocol()==compProtectedHistorySpanProtocol && r.processor()==compProtectedHistorySpanProcessorId,"Protected history protocol not selected");
                    const auto expected=Json::parse(r.bytes());
                    const auto rejected=[&](Json changed){const auto bytes=changed.dump();media_io::SampleHash hash;hash.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});
                        refuses([&]{decodeCompSpanRequest(bytes,hash.digest(),context,r.executorSha256(),ledger);});};
                    auto bad=expected;bad["clock"]["bakedPitchScale"]=Json::array({2,0,1});rejected(bad);
                    bad=expected;bad["clock"]["originalClip"]=Id::generate().str();rejected(bad);
                    bad=expected;bad["protocol"]=compProtectedSpanProtocol;bad["executor"]["id"]=compProtectedSpanProcessorId;bad.erase("clock");rejected(bad);
                    bad=expected;bad["protected"]["geometrySha256"]=std::string(64,'0');rejected(bad);
                }
                requests.push_back({{"operation",r.operation().str()},{"side",side},{"digest",r.digest()},{"file","media/comp-requests/"+r.operation().str()+".json"},{"protocol",r.protocol()}});}
            auto request=prepareCompAssemblyRequest(s,context,pair,Id::generate(),outputs,compAssemblyExecutableSha256(),{},ledger);
            const auto ref=persistCompAssemblyRequest(root,s,context,pair,request,ledger);applySessionEdits(s,{AddCompAssembly{ref}});ProjectStore(root).save(s);
            result={{"requests",requests},{"selection",pairRef.operation.str()},{"assembly",ref.operation.str()},{"mixed",mixed},{"retainedAssembledCoverageRefused",mixed},{"historySources",s.compPlaybackSources.size()},{"historicalCompletion","media/comp-assemblies/"+s.compPlaybackSources.front().assembly.operation.str()+"/complete.json"}};
        }else{
            auto s=ProjectStore(root).load();auto pair=loadReferencedCompPairSelection(root,s,context,Id(input.at("selection").get<std::string>()),ledger);
            auto request=loadReferencedCompAssemblyRequest(root,s,context,pair,Id(input.at("assembly").get<std::string>()),ledger);
            if(clocked)check(pair.spanPolicy()==CompPairSpanPolicy::ClockedProtected,"Saved clocked pair policy did not round-trip");
            if(mode=="cancel"){
                cancelCompPair(root,pair,ledger);result={{"cancelRequested",true}};
            }else if(mode=="inspect"){
                auto recovery=inspectCompPair(root,s,context,pair,ledger);Json states=Json::array();
                for(const auto &child:recovery.children)states.push_back(unsigned(child.state));
                result={{"states",states},{"candidatesReady",recovery.candidatesReady()}};
            }else if(mode=="start"){
                startCompPairChild(root,s,context,pair,input.at("side").get<unsigned>(),input.at("ready").dump(),ledger);result={{"started",true}};
            }else{
                check(mode=="adopt","Unknown repeated fixture mode");const auto registered=s;const auto bytes=read(root/"project.json");
                const auto marker=root/utf8Path(input.at("historicalCompletion").get<std::string>());const auto saved=read(marker);auto bad=Json::parse(saved);bad["requestDigest"]=std::string(64,'0');
                const auto changed=[&](auto f){write(marker,bad.dump());try{f();}catch(...){write(marker,saved);throw;}write(marker,saved);};
                // A missing original history must refuse before creating a job.
                changed([&]{refuses([&]{publishCompAssembly(root,s,context,pair,request,ledger);});});
                check(!std::filesystem::exists(root/"media/comp-assemblies"/request.operation().str()),"Rejected history created assembly job");
                if(input.value("lateFailure",false)){
                    std::array<std::optional<Id>,2> outputs;for(unsigned side=0;side<2;++side)if(pair.children()[side])outputs[side]=Id::generate();
                    auto late=prepareCompAssemblyRequest(s,context,pair,Id::generate(),outputs,compAssemblyExecutableSha256(),{},ledger);
                    bool injected=false;
                    try{refuses([&]{publishCompAssembly(root,s,context,pair,late,ledger,[&](CompAssemblyStage stage,unsigned){
                        if(stage==CompAssemblyStage::BeforeCompletion){write(marker,bad.dump());injected=true;}
                    });});}catch(...){write(marker,saved);throw;}write(marker,saved);
                    check(injected && !std::filesystem::exists(root/"media/comp-assemblies"/late.operation().str()/"complete.json"),"Late history change committed completion");
                }
                std::uint64_t protectedSamples=0,clockedSamples=0;
                if(protectedMode && !clocked)for(const auto &child:pair.children())if(child){
                    auto verified=verifyOwnedCompSpan(root,*child,policy(),ledger);
                    const std::array<Asset,2> assets{child->source(),verified.asset()};MediaCacheConfig config;
                    config.maximumOpenFiles=2;config.pageFrames=512;config.cacheBudgetBytes=1024*1024;config.registryBudgetBytes=256*1024;config.resources=ledger;
                    MediaReadCache cache(root,assets,child->source().sampleRate,config);
                    const auto &g=child->protectedSpan().geometry();
                    for(const auto &span:g.spans()){
                        const auto first=span.sourceBegin.frame+g.policy().halo,end=span.sourceEnd.frame-g.policy().halo,out=span.outputBegin.frame+g.policy().halo;
                        for(Frame offset=0;offset<end-first;offset+=512){const auto frames=std::min<Frame>(512,end-first-offset);
                            std::vector<float> raw(std::size_t(frames)*2),rendered(raw.size());
                            cache.read(0,g.map().region().rawOrigin.frame+first+offset,raw);cache.read(1,out+offset,rendered);
                            for(std::size_t i=0;i<raw.size();++i)check(std::bit_cast<std::uint32_t>(raw[i])==std::bit_cast<std::uint32_t>(rendered[i]),"Repeated protected core changed original raw samples");
                            protectedSamples+=raw.size();
                        }
                    }
                }
                if(clocked)for(const auto &child:pair.children())if(child){
                    auto verified=verifyOwnedCompSpan(root,*child,policy(),ledger);
                    const std::array<Asset,2> assets{child->source(),verified.asset()};MediaCacheConfig config;
                    config.maximumOpenFiles=2;config.pageFrames=512;config.cacheBudgetBytes=1024*1024;config.registryBudgetBytes=256*1024;config.resources=ledger;
                    MediaReadCache cache(root,assets,s.sampleRate,config);Frame at=0;std::array<float,1024> actual{};
                    renderRequestedCompSpan(*child,[&](Frame f,std::span<float> v){cache.read(0,f,v);},[&](std::span<const float> expected){
                        auto got=std::span(actual).first(expected.size());cache.read(1,at,got);
                        for(std::size_t i=0;i<expected.size();++i)check(std::bit_cast<std::uint32_t>(expected[i])==std::bit_cast<std::uint32_t>(got[i]),"Clocked pair worker differs from original geometry recomputation");
                        at+=Frame(expected.size()/2);clockedSamples+=expected.size();
                    },ledger,[]{});check(at==verified.asset().frames,"Clocked pair worker extent differs");
                }
                publishCompAssembly(root,s,context,pair,request,ledger);
                changed([&]{refuses([&]{verifyCompAssembly(root,s,context,pair,request,ledger);});
                    refuses([&]{verifyCompAssemblyAdoption(root,s,context,pair,request,ledger);});});
                auto verified=verifyCompAssemblyAdoption(root,s,context,pair,request,ledger);check(verified.ownedBy(ledger),"Repeated adoption lost joined guard");
                auto stale=s;stale.compPlaybackSources.front().selectionSha256=std::string(64,'0');const auto originalStale=stale;
                refuses([&]{applySessionEdits(stale,{verified.edit()});});check(stale==originalStale,"Stale repeated Apply mutated session");
                check(read(root/"project.json")==bytes,"Verification changed canonical project");
                const auto &transition=pair.recipe().plan().handles().transition();Frame first=registered.tracks[0].clips[0].startFrame+128;
                Frame end=registered.tracks[0].clips[1].startFrame+registered.tracks[0].clips[1].lengthFrames-128;
                const auto before=playback(root,registered,first,end);
                EditHistory history(s);check(history.structural({verified.edit()}),"Repeated canonical Apply failed");const auto adopted=s;
                check(history.undo() && s==registered,"Repeated Undo lost sources");check(history.redo() && s==adopted,"Repeated Redo lost history");
                ProjectStore(root).save(s);const auto reopened=ProjectStore(root).load();check(reopened==adopted,"Repeated save/reopen changed state");
                const auto after=playback(root,reopened,first,end);check(before.size()==after.size(),"Repeated playback extent changed");
                // Replacing a fade changes its former envelope as well as the
                // newly selected envelope. Compare audible output outside both;
                // immutable source PCM is independently checked by assembly.
                Frame editedFirst=transition.startFrame,editedEnd=transition.endFrame;
                const auto &oldOut=registered.tracks[0].clips[0],&oldIn=registered.tracks[0].clips[1];
                if(oldOut.processing.fadeOut.endFrame>oldOut.processing.fadeOut.startFrame)
                    editedFirst=std::min(editedFirst,oldOut.startFrame+std::clamp<Frame>(oldOut.processing.fadeOut.startFrame,0,oldOut.lengthFrames));
                if(oldIn.processing.fadeIn.endFrame>oldIn.processing.fadeIn.startFrame)
                    editedEnd=std::max(editedEnd,oldIn.startFrame+std::clamp<Frame>(oldIn.processing.fadeIn.endFrame,0,oldIn.lengthFrames));
                std::size_t unchanged=0;for(Frame frame=first;frame<end;++frame)if(frame<editedFirst-128 || frame>=editedEnd+128)
                    for(unsigned channel=0;channel<2;++channel){const auto i=std::size_t(frame-first)*2+channel;check(before[i]==after[i],"PCM outside repeated edit changed");++unchanged;}
                check(unchanged>0,"No unchanged PCM scope checked");
                const auto destination=root/"exports"/(request.operation().str()+".wav");ExportSpec spec(reopened.tracks[0].id);spec.startFrame=first;spec.endFrame=end;spec.blockFrames=127;
                const auto exported=exportTrackWav(root,reopened,destination,spec);
                media_io::File descriptor(destination,false);SF_INFO info{};
                std::unique_ptr<SNDFILE,decltype(&sf_close)> wave(sf_open_fd(descriptor.descriptor(),SFM_READ,&info,SF_FALSE),&sf_close);
                check(bool(wave) && info.frames==end-first && info.channels==2 && info.samplerate==int(s.sampleRate),"Repeated export geometry differs");
                std::array<float,512> block{};
                for(Frame at=0;at<info.frames;){const auto n=std::min<Frame>(256,info.frames-at);
                    check(sf_readf_float(wave.get(),block.data(),n)==n,"Repeated export read incomplete");
                    for(std::size_t i=0;i<std::size_t(n)*2;++i)
                        check(block[i]==after[std::size_t(at)*2+i],"Repeated export differs from live core");
                    at+=n;}
                check(sf_close(wave.release())==0,"Repeated export close failed");
                check(exported.peak>1,"Repeated export lost floating headroom");
                result={{"audibleEditFirst",editedFirst},{"audibleEditEnd",editedEnd},{"requestedTransitionFirst",transition.startFrame},{"requestedTransitionEnd",transition.endFrame},{"protectedSamples",protectedSamples},{"clockedSamples",clockedSamples},{"clockedProtected",clocked},{"historySources",s.compPlaybackSources.size()},{"queuedStaleApplyRefused",true},{"historyFailureBeforePublication",true},{"changedHistoryVerificationRefused",true},{"lateHistoryBeforeCompletionRefused",input.value("lateFailure",false)},{"undoRedo",true},{"savedReopened",true},{"unchangedSamples",unchanged},{"playbackEdgeGuardFrames",128},{"liveExportSamples",after.size()},{"exportSha256",exported.fileSha256},{"peak",exported.peak},{"mainWindow",false},{"qualityAdmission",false}};
            }
        }
    }
    check(!ledger.usage().reservedBytes,"Repeated workflow leaked joined resource credits");result["joinedMemoryReleased"]=true;result["checks"]=checks;std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
