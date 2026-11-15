// SPDX-License-Identifier: GPL-3.0-only
// Isolated feasibility adapter. No application, worker, publication or install hook.
#include <soundcurrent/comp_handle_plan.hpp>
#include <soundcurrent/protected_warp_render.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
Frame integer(SourcePosition p){check(!p.fraction,"Candidate refuses fractional acoustic positions");return p.frame;}
Json position(SourcePosition p){return Json::array({p.frame,p.fraction,p.denominator});}
// Rebase the original semantic anchors, never infer attack ownership from slope.
// The caller keeps envelope and original source state alive. The plan keeps its
// own charged storage. Geometry checks run before any candidate samples are read.
ProtectedWarpPlan candidate(const CompHandleEnvelope &envelope,ResourceLedger ledger){
    const auto &e=envelope.data();
    check(!e.regeneration && !e.expected.compHandles,"Candidate does not yet support history or prior handle artifacts");
    check(e.expected.stretch && e.expected.stretch->warp,"Candidate requires original protected anchors");
    check(e.raw.sampleRate==e.base.sampleRate,"Candidate refuses changed sample clocks");
    const auto &anchor=*e.expected.stretch;
    check(!anchor.context && !anchor.settings.pitchMilliCents && anchor.settings.formantPreserved,"Candidate protected settings differ");
    const auto first=e.map.front().raw,span=subtractSourcePosition(e.map.back().raw,first);
    const auto origin=integer(first),count=integer(span);
    check(origin>=0 && count>0 && origin<=e.raw.frames-count,"Candidate exceeds actual raw extent");
    auto scratch=ledger.reserve(anchor.warp->markers.size()*(sizeof(WarpAnchor)+256));
    std::vector<WarpAnchor> markers;markers.reserve(anchor.warp->markers.size());
    for(const auto &m:anchor.warp->markers)markers.push_back({m.id,
        subtractSourcePosition(addSourcePosition(anchor.sourceOrigin,m.source),first),
        addSourcePosition(m.output,{e.prefixFrames,0,1})});
    ProtectedWarpPlan plan({first,e.raw.sampleRate,e.raw.frames,count,e.outputFrames,{}, {count,0,1}},markers,anchor.warp->protection,ledger);
    validateProtectedWarpRender(plan,e.raw.sampleRate,e.raw.layout.channels);
    // Check the union of both piecewise maps' knots. Equality just at the original
    // user anchors would miss different slopes at protected-span boundaries.
    for(const auto &p:e.map)check(addSourcePosition(first,plan.map().outputToSource(p.output))==p.raw,"Rebased candidate differs at an envelope knot");
    for(const auto &p:plan.map().points()){
        const auto out=p.output;
        auto right=std::upper_bound(e.map.begin(),e.map.end(),out,[](auto x,const auto &k){return sourcePositionLess(x,k.output);});
        SourcePosition raw;
        if(right==e.map.end())raw=e.map.back().raw;
        else {check(right!=e.map.begin(),"Candidate knot before envelope");const auto &a=*(right-1),&b=*right;
            const auto ratio=sourceIntervalRatio(subtractSourcePosition(b.raw,a.raw),subtractSourcePosition(b.output,a.output));
            raw=addSourcePosition(a.raw,scaleSourcePosition(subtractSourcePosition(out,a.output),ratio.numerator,ratio.denominator));}
        check(raw==addSourcePosition(first,p.source),"Rebased candidate changes an envelope segment");
    }
    return plan;
}
Session fixture(unsigned rate,bool fractional){
    auto s=makeOneTrackSession("Protected comp candidate","Audio",rate);auto &t=s.tracks[0];t.layout={LayoutKind::Stereo,2};
    Asset raw;raw.layout=t.layout;raw.sampleRate=rate;raw.frames=98304;raw.relativePath="media/raw.wav";raw.sha256=std::string(64,'a');
    TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=raw.frames;lane.captureEndFrame=raw.frames;lane.clips={take};t.takeLanes={lane};
    ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={16384,fractional?1u:0u,fractional?7u:1u};a.sourceFrames=32768;
    a.settings=canonicalStretchSettings({3,2,0,true});WarpSettings warp;warp.protection={256,512,64,2048};
    warp.markers.push_back({Id("00000000-0000-0000-0000-000000000001"),{8192,0,1},{12288,0,1}});a.warp=warp;
    a.processor=std::string(stretchProcessorFor(a.settings,a.context,a.warp));a.renderKey=std::string(64,'b');
    Asset base=raw;base.id=Id::generate();base.relativePath="media/base.wav";base.frames=49152;base.sha256=std::string(64,'c');s.assets={raw,base};
    Clip clip;clip.assetId=base.id;clip.startFrame=10000;clip.lengthFrames=base.frames;clip.stretch=a;clip.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};t.clips={clip};validate(s);return s;
}
CompHandleEnvelope envelope(const Session &s,Frame prefix,Frame suffix,ResourceLedger ledger){const auto &t=s.tracks[0];const auto &c=t.clips[0];return prepareCompHandleEnvelope(s,t.id,c.id,c.startFrame-prefix,c.startFrame+c.lengthFrames+suffix,{1,"isolated-comp-candidate"},ledger);}
void pcm(const std::filesystem::path &path,const std::vector<float> &samples){
    std::ofstream f(path,std::ios::binary);for(float sample:samples){const auto bits=std::bit_cast<std::uint32_t>(sample);for(unsigned i=0;i<4;++i)f.put(char((bits>>(8*i))&255));}
    f.flush();check(bool(f),"Candidate PCM write failed");
}
}
int main(int argc,char **argv){try{
    check(argc==2,"Supply exclusive output directory");const std::string arg=argv[1];const std::filesystem::path root(std::u8string(arg.begin(),arg.end()));check(std::filesystem::create_directory(root),"Output directory exists");
    ResourceLedger ledger(8*1024*1024,"Isolated protected comp");Json rows=Json::array(),refusals=Json::array();
    for(unsigned rate:{44100u,48000u,96000u}){
        std::vector<float> source(98304*2);
        for(Frame f=0;f<98304;++f)for(unsigned ch=0;ch<2;++ch){const double t=double(f)-3*ch;double x=.07*std::sin(2*std::numbers::pi*997*t/rate);
            const double event=t-24576;if(event>=0 && event<1024)x+=2.5*std::exp(-event/192)*std::cos(2*std::numbers::pi*1703*event/rate);
            source[std::size_t(f)*2+ch]=float(ch?-.5*x:x);}
        pcm(root/("source-"+std::to_string(rate)+".f32le"),source);
        for(unsigned extension:{0u,1u,2u}){
            const auto s=fixture(rate,false),original=s;
            {
                const auto e=envelope(s,3008*extension,4544*extension,ledger);const auto &d=e.data();auto plan=candidate(e,ledger);
                const auto rawOrigin=integer(plan.map().region().rawOrigin),rawCount=plan.map().region().inputFrames;
                std::vector<float> output;output.reserve(std::size_t(d.outputFrames)*2);unsigned reads=0,writes=0,polls=0;
                const auto read=[&](Frame at,std::span<float> out){++reads;check(at>=0 && out.size()%2==0 && at<=rawCount-Frame(out.size()/2),"Candidate relative read outside actual source");std::copy_n(source.begin()+(rawOrigin+at)*2,out.size(),out.begin());};
                const auto write=[&](std::span<const float> in){++writes;output.insert(output.end(),in.begin(),in.end());};
                const auto report=renderProtectedWarp(plan,rate,2,read,write,ledger,[&]{++polls;});
                check(report.writtenFrames==d.outputFrames && output.size()==std::size_t(d.outputFrames)*2,"Candidate duration mismatch");
                Json points=Json::array(),replanned=Json::array(),cores=Json::array();
                for(const auto &p:d.map)points.push_back({position(p.raw),position(p.output)});
                for(const auto &p:plan.map().points())replanned.push_back({position(addSourcePosition(plan.map().region().rawOrigin,p.source)),position(p.output)});
                for(const auto &p:plan.spans()){
                    const auto sb=integer(p.sourceBegin)+plan.policy().halo,se=integer(p.sourceEnd)-plan.policy().halo,ob=integer(p.outputBegin)+plan.policy().halo;
                    for(Frame i=0;i<se-sb;++i)for(unsigned ch=0;ch<2;++ch)check(std::bit_cast<std::uint32_t>(source[std::size_t(rawOrigin+sb+i)*2+ch])==std::bit_cast<std::uint32_t>(output[std::size_t(ob+i)*2+ch]),"Protected core changed samples");
                    cores.push_back({{"rawBegin",rawOrigin+sb},{"outputBegin",ob},{"frames",se-sb},{"owner",p.owner.str()}});
                }
                if(rate==44100 && extension==1){
                    const auto credit=ledger.usage().reservedBytes;
                    unsigned emitted=0;bool canceled=false;
                    try{renderProtectedWarp(plan,rate,2,read,[&](auto){++emitted;},ledger,[]{throw ProjectError(ErrorCode::Canceled,"Candidate cancellation");});}catch(const ProjectError &error){canceled=error.code()==ErrorCode::Canceled;}
                    check(canceled && !emitted && ledger.usage().reservedBytes==credit,"Candidate cancellation wrote or leaked");
                    ResourceLedger tiny(1,"Candidate tiny budget");bool denied=false;
                    try{renderProtectedWarp(plan,rate,2,read,[&](auto){++emitted;},tiny,[]{});}catch(const ProjectError &){denied=true;}
                    check(denied && !emitted && !tiny.usage().reservedBytes,"Candidate budget wrote or leaked");
                    refusals.push_back("cancellation-before-output");refusals.push_back("render-budget-before-output");
                }
                const auto name="output-"+std::to_string(rate)+"-"+std::to_string(extension)+".f32le";pcm(root/name,output);
                rows.push_back({{"rate",rate},{"extension",extension},{"rawOrigin",rawOrigin},{"rawFrames",rawCount},{"outputFrames",d.outputFrames},{"prefix",d.prefixFrames},{"suffix",d.suffixFrames},{"envelope",points},{"candidate",replanned},{"cores",cores},{"processedGaps",report.processedGaps},{"reads",reads},{"writes",writes},{"polls",polls},{"pcm",name}});
                check(s==original,"Candidate changed source/project state");
            }
            check(!ledger.usage().reservedBytes,"Candidate retained resource credit");
        }
    }
    for(bool fractional:{false,true}){
        auto s=fixture(48000,fractional);bool refused=false;
        {auto e=envelope(s,fractional?3008:1,4544,ledger);try{auto p=candidate(e,ledger);(void)p;}catch(const std::runtime_error &error){refused=std::string(error.what())=="Candidate refuses fractional acoustic positions";}}
        check(refused && !ledger.usage().reservedBytes,"Fractional candidate accepted or leaked");refusals.push_back(fractional?"fractional-original-origin":"fractional-extended-origin");
    }
    Json result{{"format","sc-comp-protected-candidate-v1"},{"rows",rows},{"refusals",refusals},{"shippingEnabled",false},{"qualityAdmission",false},{"nativeWindowsQualified",false}};
    std::ofstream f(root/"report.json");f<<result.dump(2)<<'\n';f.flush();check(bool(f),"Candidate report failed");std::cout<<Json{{"renders",rows.size()},{"refusals",refusals},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
