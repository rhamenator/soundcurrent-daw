// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_processing_plan.hpp>
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
unsigned checks=0;
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
    auto in=c;in.id=Id::generate();in.startFrame=c.startFrame+c.lengthFrames;t.clips={c,in};validate(s);return s;
}
CompHandlePcmRead reader(const std::vector<float> &v){return [&v](Frame at,std::span<float> out){check(at>=0 && std::size_t(at)<=v.size() && out.size()<=v.size()-std::size_t(at),"PCM permission read overflow");std::copy_n(v.begin()+at,out.size(),out.begin());};}
std::vector<float> resample(const std::vector<float> &v,const SourceFrameMap &map,Frame frames){
    PreparedPositionedResampling k(map,1);std::vector<float> out(std::size_t(frames),0);
    for(Frame at=0;at<frames;at+=113){const auto n=std::uint32_t(std::min<Frame>(113,frames-at));const auto r=k.sourceRange(map,at,n,Frame(v.size()));
        k.process(map,at,Frame(v.size()),r.first,std::span(v).subspan(std::size_t(r.first),r.frames),std::span(out).subspan(std::size_t(at),n));}
    return out;
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
            const std::array<const CompHandleEnvelope *,2> env{&p.handles().outgoing(),&p.handles().incoming()};
            const std::array<const CompFragmentProcessing *,2> views{&p.outgoing(),&p.incoming()};
            for(unsigned side=0;side<2;++side){const auto &h=env[side]->data();const auto &v=*views[side];
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
    auto small=fixture(48000,{1,1},false);auto &t=small.tracks[0];const auto boundary=t.clips[1].startFrame;
    ResourceLedger ledger(8*1024*1024,"One frame");auto p=prepareCompProcessing(small,{t.id,t.clips[0].id,t.clips[1].id,boundary-1,boundary+1,ClipFadeCurve::Linear},{1,small.id.str()},8,ledger);
    check(p.outgoing().blends.suffix->frames==1 && p.incoming().blends.prefix->frames==1,"One-frame permitted region silently widened");
    auto raw=small;for(auto &clip:raw.tracks[0].clips){clip.stretch.reset();clip.assetId=raw.assets[0].id;clip.sourceFrame=1024;}
    validate(raw);auto existing=prepareCompProcessing(raw,p.handles().transition(),{1,raw.id.str()},8,ledger);
    check(existing.outgoing().domain==CompProcessingDomain::ExistingMedia && existing.incoming().domain==CompProcessingDomain::ExistingMedia,"Raw existing handles created processing permissions");
    ResourceLedger tiny(1,"Tiny");refuses([&]{prepareCompProcessing(small,p.handles().transition(),{1,small.id.str()},8,tiny);});check(!tiny.usage().reservedBytes,"Permission resource refusal leaked");
    std::cout<<"comp_processing_checks="<<checks<<" physical_cases="<<physical<<" project_after_src_cases="<<project<<" outside_project_pcm_exact=true actual_renderer=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
