// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_processing_plan.hpp>
#include <soundcurrent/comp_playback_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <type_traits>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0,preparedSources=0,persistedPairs=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F> void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid permission accepted");}
Session fixture(unsigned rate,ClipPlaybackRate speed,bool fractional,Frame offset=0){
    auto s=makeOneTrackSession("Processing permission","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Mono,1};
    Asset raw;raw.relativePath="media/raw.wav";raw.sampleRate=rate;raw.frames=8192;raw.sha256=std::string(64,'a');raw.layout=t.layout;s.assets={raw};
    TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=SourceFrameMap(rate,48000).projectFramesForSource(raw.frames);lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
    ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={1024,1,7};a.sourceFrames=512;a.settings={3,2,0,true};a.renderKey=std::string(64,'b');a.processor=stretchProcessorFor(a.settings);
    Asset base=raw;base.id=Id::generate();base.relativePath="media/render.wav";base.frames=768;base.sha256=std::string(64,'c');s.assets.push_back(base);
    Clip c;c.assetId=base.id;c.startFrame=100;c.sourceFrame=offset;c.playbackRate=speed;c.stretch=a;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};if(fractional)c.sourceTiming={1,3};
    auto map=clipSourceMap(c,rate,48000);c.lengthFrames=map.projectFramesForSource(base.frames-offset);while(map.at(c.lengthFrames-1).frame>=base.frames)--c.lengthFrames;
    c.processing.gainDb=3;c.processing.polarityInverted=true;
    c.processing.fadeIn={1,32,ClipFadeCurve::Smoothstep,1};
    c.processing.fadeOut={c.lengthFrames-32,c.lengthFrames,ClipFadeCurve::EqualPower,1};
    auto in=c;in.id=Id::generate();in.startFrame=c.startFrame+c.lengthFrames;t.clips={c,in};validate(s);return s;
}
CompHandlePcmRead reader(const std::vector<float> &v){return [&v](Frame at,std::span<float> out){check(at>=0 && std::size_t(at)<=v.size() && out.size()<=v.size()-std::size_t(at),"PCM permission read overflow");std::copy_n(v.begin()+at,out.size(),out.begin());};}
std::vector<float> resample(const std::vector<float> &v,const SourceFrameMap &map,Frame frames){
    PreparedPositionedResampling k(map,1);std::vector<float> out(std::size_t(frames),0);
    for(Frame at=0;at<frames;at+=113){const auto n=std::uint32_t(std::min<Frame>(113,frames-at));const auto r=k.sourceRange(map,at,n,Frame(v.size()));
        k.process(map,at,Frame(v.size()),r.first,std::span(v).subspan(std::size_t(r.first),r.frames),std::span(out).subspan(std::size_t(at),n));}
    return out;
}
void playbackSource(const Session &s,const CompHandlePlanContext &context,const CompProcessingPlan &p,unsigned side,ResourceLedger ledger) {
    const auto &h=(side?p.handles().incoming():p.handles().outgoing()).data();
    const auto &v=side?p.incoming():p.outgoing();
    const bool generated=v.domain!=CompProcessingDomain::ExistingMedia;
    const bool post=v.domain==CompProcessingDomain::ProjectAfterSrc;
    std::optional<Asset> output;
    if(generated) {
        output=h.base;output->id=Id::generate();output->relativePath="media/final.wav";
        output->frames=post?h.projectEnd-h.projectFirst:h.outputFrames;
        output->sampleRate=post?h.projectRate:h.base.sampleRate;
        output->sha256=std::string(64,'d');
    }
    const auto reserved=ledger.usage().reservedBytes;
    {
        auto prepared=prepareCompPlaybackSource(s,context,p,side,output,ledger);
        ++preparedSources;
        const auto &d=prepared.data();const auto &c=d.playback;
        check(d.original==h.expected && d.raw==h.raw && d.base==h.base && d.nominal==h.nominalAsset,
              "Playback preparation lost original raw/nominal metadata");
        check(c.takeOrigin==h.expected.takeOrigin && c.id==h.expected.id,"Playback lost retained source identity");
        check(c.startFrame==h.projectFirst && c.lengthFrames==h.projectEnd-h.projectFirst,"Playback timeline shifted");
        check(!generated || (!c.stretch && !c.compHandles),"Nominal anchors incorrectly relabeled as final output");
        check(!post || (c.sourceFrame==0 && c.sourceTiming==ClipSourceTiming{} && c.playbackRate==ClipPlaybackRate{}),
              "Post-SRC playback would apply rate conversion twice");
        const auto playback=clipSourceMap(c,d.rendered.sampleRate,s.sampleRate);
        const auto original=clipSourceMap(h.expected,h.base.sampleRate,s.sampleRate);
        const auto offset=h.expected.startFrame-h.projectFirst;
        for(Frame f=0;f<h.expected.lengthFrames;++f)
            check(prepared.rawPosition(playback.at(offset+f))==processedClipOutputToRaw(h.expected,h.raw,original.at(f)),
                  "Prepared playback changed original sample's raw provenance");
        const auto middle=h.expected.lengthFrames/2;
        const auto finalFraction=addSourcePosition(playback.at(offset+middle),scaleSourcePosition({0,1,2},playback.numerator(),playback.denominator()));
        const auto originalFraction=addSourcePosition(original.at(middle),scaleSourcePosition({0,1,2},original.numerator(),original.denominator()));
        check(prepared.rawPosition(finalFraction)==processedClipOutputToRaw(h.expected,h.raw,originalFraction),
              "Fractional playback crop lost exact raw provenance");
        check(prepared.rawPosition(playback.at(0))==h.requestedRawFirst &&
              prepared.rawPosition(playback.at(c.lengthFrames-1))==h.requestedRawLast,
              "Prepared playback changed extended raw endpoints");
        auto expectedProcessing=h.expected.processing;
        shiftClipProcessing(expectedProcessing,h.projectFirst-h.expected.startFrame);
        check(c.processing==expectedProcessing,"Playback moved original processing anchors");
        refuses([&]{prepared.rawPosition({d.rendered.frames,1,2});});
        refuses([&]{prepared.rawPosition({0,1,0});});
        auto moved=std::move(prepared);refuses([&]{prepared.data();});check(moved.data().original==h.expected,"Moved playback lost source");
    }
    check(ledger.usage().reservedBytes==reserved,"Playback source leaked its resource reservation");
    ResourceLedger tiny(1,"Playback source quota");refuses([&]{prepareCompPlaybackSource(s,context,p,side,output,tiny);});
    check(!tiny.usage().reservedBytes,"Rejected playback source leaked credit");
    refuses([&]{prepareCompPlaybackSource(s,context,p,2,output,ledger);});
    auto stale=context;++stale.projectEpoch;
    refuses([&]{prepareCompPlaybackSource(s,stale,p,side,output,ledger);});
    if(output) {
        auto bad=*output;bad.frames++;refuses([&]{prepareCompPlaybackSource(s,context,p,side,bad,ledger);});
        bad=*output;bad.sampleRate=output->sampleRate==48000?44100:48000;refuses([&]{prepareCompPlaybackSource(s,context,p,side,bad,ledger);});
        bad=*output;bad.sha256="unverified";refuses([&]{prepareCompPlaybackSource(s,context,p,side,bad,ledger);});
        bad=*output;bad.id=h.base.id;refuses([&]{prepareCompPlaybackSource(s,context,p,side,bad,ledger);});
        bad=*output;bad.relativePath="../escape.wav";refuses([&]{prepareCompPlaybackSource(s,context,p,side,bad,ledger);});
        refuses([&]{prepareCompPlaybackSource(s,context,p,side,{},ledger);});
    } else refuses([&]{prepareCompPlaybackSource(s,context,p,side,h.base,ledger);});
}
void persistentPair(const Session &initial,const CompHandlePlanContext &context,const CompProcessingPlan &plan,ResourceLedger ledger) {
    if(plan.outgoing().domain==CompProcessingDomain::ExistingMedia && plan.incoming().domain==CompProcessingDomain::ExistingMedia)return;
    auto s=initial;CompAssemblyReference reference{Id::generate(),Id::generate(),std::string(64,'e'),{}};
    std::array<std::optional<Asset>,2> outputs;
    for(unsigned side=0;side<2;++side){
        const auto &h=(side?plan.handles().incoming():plan.handles().outgoing()).data();
        const auto &p=side?plan.incoming():plan.outgoing();if(p.domain==CompProcessingDomain::ExistingMedia)continue;
        auto a=h.base;a.id=Id::generate();a.sha256=std::string(64,'d');reference.outputs[side]=a.id;
        a.relativePath="media/comp-results/"+reference.selectionOperation.str()+"/"+a.id.str()+".wav";
        a.sampleRate=p.domain==CompProcessingDomain::ProjectAfterSrc?h.projectRate:h.base.sampleRate;
        a.frames=p.domain==CompProcessingDomain::ProjectAfterSrc?h.projectEnd-h.projectFirst:h.outputFrames;
        outputs[side]=a;
    }
    applySessionEdits(s,{AddCompPairSelection{{reference.selectionOperation,std::string(64,'a')}},AddCompAssembly{reference}});
    const auto identity=[](const CompHandleEnvelopeData &h){return CompAssemblySourceIdentity{h.expected,h.retained,h.raw,h.base,h.retainedAsset,h.nominalAsset};};
    const auto registered=s;ApplyCompAssembly edit{plan.handles().transition(),reference,s.compPairSelections.back(),
        {identity(plan.handles().outgoing().data()),identity(plan.handles().incoming().data())},{}};
    for(unsigned side=0;side<2;++side)if(outputs[side])edit.sources[side]=prepareCompPlaybackSource(s,context,plan,side,outputs[side],ledger).data();
    EditHistory history(s);check(history.structural({edit}),"Paired comp adoption was not recorded");++persistedPairs;
    const auto adopted=s;
    std::size_t provenanceBytes=0;for(const auto &source:s.compPlaybackSources)provenanceBytes+=compPlaybackSourcePayloadBytes(source);
    check(history.resources().retainedBytes>=provenanceBytes,"History omitted comp provenance payload");
    check(s.compAssemblies.empty() && s.compPlaybackSources.size()==std::size_t(bool(outputs[0]))+std::size_t(bool(outputs[1])),"Adoption did not consume exact reservations");
    check(s.tracks[0].takeLanes==initial.tracks[0].takeLanes,"Adoption changed retained takes");
    const auto bytes=encodeProject(s);auto document=nlohmann::json::parse(bytes);
    check(document.at("schemaMinor")==23 && decodeProject(bytes)==s,"Playback provenance roundtrip changed state");
    check(history.undo() && s==registered,"Comp adoption Undo lost original state/reservations");
    check(history.redo() && s==adopted,"Comp adoption Redo lost playback provenance");
    auto current=s;
    auto malformed=edit;malformed.expected[0].clip.processing.gainDb++;
    auto rejected=registered;refuses([&]{applySessionEdits(rejected,{malformed});});check(rejected==registered,"Rejected pair changed first fragment");
    malformed=edit;malformed.expected[1].retained.processing.gainDb++;
    refuses([&]{applySessionEdits(rejected,{malformed});});check(rejected==registered,"Stale retained take changed first fragment");
    malformed=edit;malformed.expected[1].base.sha256=std::string(64,'0');
    refuses([&]{applySessionEdits(rejected,{malformed});});check(rejected==registered,"Stale asset changed first fragment");
    malformed=edit;malformed.expectedSelection.selectionSha256=std::string(64,'0');
    refuses([&]{applySessionEdits(rejected,{malformed});});check(rejected==registered,"Stale selection consumed request");
    malformed=edit;malformed.sources[outputs[0]?0:1].reset();
    refuses([&]{applySessionEdits(rejected,{malformed});});check(rejected==registered,"Incomplete pair consumed reservations");
    refuses([&]{applySessionEdits(current,{RemoveCompPairSelection{reference.selectionOperation}});});check(current==s,"Failed parent removal changed state");
    for(unsigned mode=0;mode<8;++mode){auto bad=document;auto &record=bad["compPlaybackSources"][0];
        if(mode==0)record["expandedDenominator"]=0;
        if(mode==1)record["rawMap"][0]["raw"]["frame"]=0;
        if(mode==2)record["protocol"]="unsupported";
        if(mode==3)record["baseAsset"]["sha256"]=std::string(64,'0');
        if(mode==4)record["original"]["sourceTiming"]["denominator"]=0;
        if(mode==5)record["side"]=2;
        if(mode==6)record["extra"]=true;
        if(mode==7)bad["compPlaybackSources"].push_back(record);
        refuses([&]{decodeProject(bad.dump());});
    }
    auto duplicate=s;duplicate.compAssemblies.push_back(reference);refuses([&]{validate(duplicate);});
    if(s.compPlaybackSources.size()==2){
        auto half=s;half.compPlaybackSources.pop_back();refuses([&]{validate(half);});
        std::swap(s.compPlaybackSources[0],s.compPlaybackSources[1]);validate(s);const auto conflicted=s;
        refuses([&]{history.undo();});check(s==conflicted,"Conflicting history edit partially applied");s=adopted;
    }
    const auto clip=s.tracks[0].clips[0];
    applySessionEdits(s,{CropClip{s.tracks[0].id,clip.id,clip.startFrame+2,2,clip.lengthFrames-4}});
    const auto cropped=s.tracks[0].clips[0];
    applySessionEdits(s,{SplitClip{s.tracks[0].id,cropped.id,Id::generate(),cropped.startFrame+cropped.lengthFrames/2}});
    applySessionEdits(s,{SetClipPlaybackRate{s.tracks[0].id,cropped.id,{3,2}}});
    check(decodeProject(encodeProject(s))==s,"Edited playback lost persisted raw provenance");
    check(s.compPlaybackSources==adopted.compPlaybackSources,"Crop/split/rate rewrote immutable source provenance");
    StateBudget tiny;tiny.memoryBudgetBytes=sizeof(Session);refuses([&]{validate(adopted,tiny);});
    HistoryBudget historyLimit;historyLimit.retainedBytes=1;
    auto limited=registered;EditHistory limitedHistory(limited,{},historyLimit);
    refuses([&]{limitedHistory.structural({edit});});check(limited==registered && !limitedHistory.resources().undoCommands,"History quota partially adopted a pair");
}
}
int main(){try{
    static_assert(!std::is_copy_constructible_v<CompProcessingPlan>);
    static_assert(!std::is_move_assignable_v<CompProcessingPlan>);
    unsigned physical=0,project=0;
    for(const unsigned rate:{44100,48000,96000})for(const ClipPlaybackRate speed:{ClipPlaybackRate{1,1},ClipPlaybackRate{3,2}})for(bool fraction:{false,true})for(const Frame half:{26,256})for(const Frame offset:{0,64}){
        auto s=fixture(rate,speed,fraction,offset);const auto original=s;const auto &t=s.tracks[0];const auto boundary=t.clips[1].startFrame;
        const auto extent=std::min(half,t.clips[0].lengthFrames-1);
        PrepareCompTransition e{t.id,t.clips[0].id,t.clips[1].id,boundary-extent,boundary+extent,ClipFadeCurve::EqualPower};
        const CompHandlePlanContext context{7,s.id.str()};ResourceLedger ledger(8*1024*1024,"Permission test");
        {
            auto p=prepareCompProcessing(s,e,context,8,ledger);verifyCompProcessingCurrent(s,context,p);check(s==original,"Permission preparation modified project");
            persistentPair(s,context,p,ledger);
            const std::array<const CompHandleEnvelope *,2> env{&p.handles().outgoing(),&p.handles().incoming()};
            const std::array<const CompFragmentProcessing *,2> views{&p.outgoing(),&p.incoming()};
            for(unsigned side=0;side<2;++side){const auto &h=env[side]->data();const auto &v=*views[side];
                playbackSource(s,context,p,side,ledger);
                if(v.domain==CompProcessingDomain::ExistingMedia){check(!h.needsNewSpan,"Missing handle became existing media");continue;}
                std::vector<float> old(std::size_t(h.base.frames));for(std::size_t i=0;i<old.size();++i)old[i]=float(2.5*std::sin(double(i)*.027));
                std::vector<float> candidate(std::size_t(h.outputFrames));for(std::size_t i=0;i<candidate.size();++i)candidate[i]=float(-1.7*std::cos(double(i)*.043));
                auto oldMap=clipSourceMap(h.expected,h.base.sampleRate,h.projectRate);
                SourceFrameMap newMap(h.base.sampleRate,h.projectRate,h.clipOutputOrigin,h.expected.playbackRate);
                const auto expected=resample(old,oldMap,h.expected.lengthFrames);
                std::vector<float> actual;
                if(v.domain==CompProcessingDomain::PhysicalArtifact){++physical;
                    std::vector<float> assembled;blendCompHandlePcm(v.pcm,v.blends,reader(old),reader(candidate),[&](auto values){assembled.insert(assembled.end(),values.begin(),values.end());},ledger,[]{});
                    auto aligned=newMap.advanced(h.expected.startFrame-h.projectFirst);actual=resample(assembled,aligned,h.expected.lengthFrames);
                }else{++project;
                    const auto newProject=resample(candidate,newMap,h.projectEnd-h.projectFirst);
                    std::vector<float> assembled;blendCompHandlePcm(v.pcm,v.blends,reader(expected),reader(newProject),[&](auto values){assembled.insert(assembled.end(),values.begin(),values.end());},ledger,[]{});
                    actual.assign(assembled.begin()+v.pcm.prefixFrames,assembled.begin()+v.pcm.prefixFrames+h.expected.lengthFrames);
                }
                check(actual.size()==expected.size(),"Processing changed original project extent");
                for(Frame f=0;f<h.expected.lengthFrames;++f){const auto at=h.expected.startFrame+f;
                    if(at<e.startFrame || at>=e.endFrame)check(std::bit_cast<std::uint32_t>(actual[std::size_t(f)])==std::bit_cast<std::uint32_t>(expected[std::size_t(f)]),"Processing leaked outside requested project window");}
            }
            auto stale=context;++stale.projectEpoch;refuses([&]{verifyCompProcessingCurrent(s,stale,p);});
            auto changed=s;changed.tracks[0].clips[1].processing.gainDb=1;refuses([&]{verifyCompProcessingCurrent(changed,context,p);});
            auto moved=std::move(p);refuses([&]{(void)p.outgoing();});verifyCompProcessingCurrent(s,context,moved);
        }
        check(ledger.usage().reservedBytes==0,"Processing plan retained credit");
        refuses([&]{prepareCompProcessing(s,e,context,0,ledger);});
        check(ledger.usage().reservedBytes==0 && s==original,"Refused permission changed state or leaked credit");
    }
    check(physical>0 && project>0,"Permission corpus did not exercise both processing domains");
    // Preserve protected nonuniform timing and already-expanded nominal windows.
    // This exercises provenance geometry, not a nonuniform audio producer.
    for(bool priorHandles:{false,true})for(unsigned rate:{44100,48000,96000}) {
        auto layered=fixture(rate,{3,2},true);auto &track=layered.tracks[0];
        if(priorHandles) {
            const auto nominal=layered.assets[1];auto expanded=nominal;
            expanded.id=Id::generate();expanded.frames+=64;expanded.sha256=std::string(64,'e');
            expanded.relativePath="media/prior-expanded.wav";layered.assets.push_back(expanded);
            for(auto &c:track.clips) {
                c.assetId=expanded.id;c.sourceFrame+=32;
                c.compHandles=ClipCompHandleWindow{nominal.id,nominal.sha256,std::string(64,'f'),32,32};
            }
        } else for(auto &c:track.clips) {
            WarpSettings warp;warp.protection={8,16,2,8};
            warp.markers.push_back({Id::generate(),{256,0,1},{384,0,1}});
            c.stretch->warp=warp;c.stretch->processor=stretchProcessorFor(c.stretch->settings,c.stretch->context,warp);
        }
        validate(layered);ResourceLedger resources(8*1024*1024,"Layered provenance");
        const CompHandlePlanContext context{8,layered.id.str()};const auto boundary=track.clips[1].startFrame;
        auto plan=prepareCompProcessing(layered,{track.id,track.clips[0].id,track.clips[1].id,boundary-64,boundary+64,ClipFadeCurve::EqualPower},context,8,resources);
        playbackSource(layered,context,plan,0,resources);playbackSource(layered,context,plan,1,resources);
        persistentPair(layered,context,plan,resources);
    }
    auto small=fixture(48000,{1,1},false);auto &t=small.tracks[0];const auto boundary=t.clips[1].startFrame;
    ResourceLedger ledger(8*1024*1024,"One frame");auto p=prepareCompProcessing(small,{t.id,t.clips[0].id,t.clips[1].id,boundary-1,boundary+1,ClipFadeCurve::Linear},{1,small.id.str()},8,ledger);
    check(p.outgoing().blends.suffix->frames==1 && p.incoming().blends.prefix->frames==1,"One-frame permitted region silently widened");
    auto raw=small;for(auto &clip:raw.tracks[0].clips){clip.stretch.reset();clip.assetId=raw.assets[0].id;clip.sourceFrame=1024;}
    validate(raw);auto existing=prepareCompProcessing(raw,p.handles().transition(),{1,raw.id.str()},8,ledger);
    check(existing.outgoing().domain==CompProcessingDomain::ExistingMedia && existing.incoming().domain==CompProcessingDomain::ExistingMedia,"Raw existing handles created processing permissions");
    playbackSource(raw,{1,raw.id.str()},existing,0,ledger);playbackSource(raw,{1,raw.id.str()},existing,1,ledger);
    ResourceLedger tiny(1,"Tiny");refuses([&]{prepareCompProcessing(small,p.handles().transition(),{1,small.id.str()},8,tiny);});check(!tiny.usage().reservedBytes,"Permission resource refusal leaked");
    std::cout<<"comp_processing_checks="<<checks<<" physical_cases="<<physical<<" project_after_src_cases="<<project<<" prepared_playback_sources="<<preparedSources<<" persisted_pairs="<<persistedPairs<<" outside_project_pcm_exact=true actual_renderer=false canonical_adoption=true\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
