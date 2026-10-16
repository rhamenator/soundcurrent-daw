// SPDX-License-Identifier: GPL-3.0-only
// Owned core acceptance; no application, worker publication or audio endpoints.
#include <soundcurrent/comp_protected_span.hpp>
#include <type_traits>
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
Session fixture(unsigned rate,bool fractional,unsigned anchors=1){
    auto s=makeOneTrackSession("Protected comp candidate","Audio",rate);auto &t=s.tracks[0];t.layout={LayoutKind::Stereo,2};
    Asset raw;raw.layout=t.layout;raw.sampleRate=rate;raw.frames=98304;raw.relativePath="media/raw.wav";raw.sha256=std::string(64,'a');
    TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=raw.frames;lane.captureEndFrame=raw.frames;lane.clips={take};t.takeLanes={lane};
    ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={16384,fractional?1u:0u,fractional?7u:1u};a.sourceFrames=32768;
    a.settings=canonicalStretchSettings({3,2,0,true});WarpSettings warp;warp.protection={256,512,64,2048};
    warp.markers.push_back({Id("00000000-0000-0000-0000-000000000001"),{8192,0,1},{12288,0,1}});if(anchors==2)warp.markers.push_back({Id("00000000-0000-0000-0000-000000000002"),{24576,0,1},{36864,0,1}});a.warp=warp;
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
    static_assert(!std::is_copy_constructible_v<CompProtectedSpanPlan> && !std::is_move_assignable_v<CompProtectedSpanPlan>);
    ResourceLedger ledger(8*1024*1024,"Owned protected comp");Json rows=Json::array(),refusals=Json::array();
    for(unsigned rate:{44100u,48000u,96000u}){
        std::vector<float> source(98304*2);
        for(Frame f=0;f<98304;++f)for(unsigned ch=0;ch<2;++ch){const double t=double(f)-3*ch;double x=.07*std::sin(2*std::numbers::pi*997*t/rate);
            for(const double event:{t-24576,t-40960})if(event>=0 && event<1024)x+=2.5*std::exp(-event/192)*std::cos(2*std::numbers::pi*1703*event/rate);
            source[std::size_t(f)*2+ch]=float(ch?-.5*x:x);}
        pcm(root/("source-"+std::to_string(rate)+".f32le"),source);
        for(unsigned anchors:{1u,2u})for(unsigned extension:{0u,1u,2u}){
            const auto s=fixture(rate,false,anchors),original=s;
            {
                const auto e=envelope(s,3008*extension,(anchors==1?4544:1472)*extension,ledger);const auto &d=e.data();auto owned=prepareProtectedCompSpan(e,ledger,[]{});const auto &plan=owned.geometry();
                check(owned.source()==d.raw && std::equal(owned.rawMap().begin(),owned.rawMap().end(),d.map.begin(),d.map.end()),"Owned source/map differ");
                const auto rawOrigin=integer(plan.map().region().rawOrigin),rawCount=plan.map().region().inputFrames;
                std::vector<float> output;output.reserve(std::size_t(d.outputFrames)*2);unsigned reads=0,writes=0,polls=0;
                const auto read=[&](Frame at,std::span<float> out){++reads;check(at>=0 && out.size()%2==0 && at>=rawOrigin && at<=rawOrigin+rawCount-Frame(out.size()/2),"Absolute read outside owned source");std::copy_n(source.begin()+at*2,out.size(),out.begin());};
                const auto write=[&](std::span<const float> in){++writes;output.insert(output.end(),in.begin(),in.end());};
                const auto report=renderProtectedCompSpan(owned,read,write,ledger,[&]{++polls;});
                check(report.writtenFrames==d.outputFrames && output.size()==std::size_t(d.outputFrames)*2,"Candidate duration mismatch");
                Json points=Json::array(),replanned=Json::array(),cores=Json::array();
                for(const auto &p:d.map)points.push_back({position(p.raw),position(p.output)});
                for(const auto &p:plan.map().points())replanned.push_back({position(addSourcePosition(plan.map().region().rawOrigin,p.source)),position(p.output)});
                for(const auto &p:plan.spans()){
                    const auto sb=integer(p.sourceBegin)+plan.policy().halo,se=integer(p.sourceEnd)-plan.policy().halo,ob=integer(p.outputBegin)+plan.policy().halo;
                    for(Frame i=0;i<se-sb;++i)for(unsigned ch=0;ch<2;++ch)check(std::bit_cast<std::uint32_t>(source[std::size_t(rawOrigin+sb+i)*2+ch])==std::bit_cast<std::uint32_t>(output[std::size_t(ob+i)*2+ch]),"Protected core changed samples");
                    cores.push_back({{"rawBegin",rawOrigin+sb},{"outputBegin",ob},{"frames",se-sb},{"owner",p.owner.str()}});
                }
                if(rate==44100 && anchors==1 && extension==1){
                    const auto credit=ledger.usage().reservedBytes;
                    unsigned emitted=0;bool canceled=false;
                    try{renderProtectedCompSpan(owned,read,[&](auto){++emitted;},ledger,[]{throw ProjectError(ErrorCode::Canceled,"Candidate cancellation");});}catch(const ProjectError &error){canceled=error.code()==ErrorCode::Canceled;}
                    check(canceled && !emitted && ledger.usage().reservedBytes==credit,"Candidate cancellation wrote or leaked");
                    ResourceLedger tiny(1,"Candidate tiny budget");bool denied=false;
                    try{renderProtectedCompSpan(owned,read,[&](auto){++emitted;},tiny,[]{});}catch(const ProjectError &error){denied=error.code()==ErrorCode::ResourceLimit;}
                    check(denied && !emitted && !tiny.usage().reservedBytes,"Candidate budget wrote or leaked");
                    refusals.push_back("cancellation-before-output");refusals.push_back("render-budget-before-output");
                    const auto refuses=[&](ErrorCode code,const char *name,const auto &action){
                        bool caught=false;try{action();}catch(const ProjectError &error){caught=error.code()==code;}
                        check(caught && ledger.usage().reservedBytes==credit,"Expected refusal code or released resources differ");refusals.push_back(name);
                    };
                    refuses(ErrorCode::MediaMismatch,"incomplete-reader",[&]{renderProtectedCompSpan(owned,[](Frame,std::span<float>){},[&](auto){++emitted;},ledger,[]{});});
                    check(!emitted,"Incomplete first read emitted output");
                    refuses(ErrorCode::MediaMismatch,"nonfinite-reader",[&]{renderProtectedCompSpan(owned,[&](Frame at,std::span<float> slab){read(at,slab);slab[0]=std::numeric_limits<float>::infinity();},[&](auto){++emitted;},ledger,[]{});});
                    check(!emitted,"Nonfinite first read emitted output");
                    unsigned partialWrites=0;
                    refuses(ErrorCode::Io,"writer-failed-after-partial-output",[&]{renderProtectedCompSpan(owned,read,[&](auto){if(++partialWrites==2)throw ProjectError(ErrorCode::Io,"Owned test writer failed");},ledger,[]{});});
                    check(partialWrites==2,"Writer failure did not exercise partial output");
                    partialWrites=0;
                    refuses(ErrorCode::Canceled,"cancel-after-partial-output",[&]{renderProtectedCompSpan(owned,read,[&](auto){++partialWrites;},ledger,[&]{if(partialWrites)throw ProjectError(ErrorCode::Canceled,"Owned test canceled after write");});});
                    check(partialWrites==1,"Canceled render wrote after cancellation");
                    unsigned prepPolls=0;
                    refuses(ErrorCode::Canceled,"cancel-during-preparation",[&]{auto other=prepareProtectedCompSpan(e,ledger,[&]{if(++prepPolls==3)throw ProjectError(ErrorCode::Canceled,"Owned preparation canceled");});(void)other;});
                    refuses(ErrorCode::ResourceLimit,"prepare-budget",[&]{auto other=prepareProtectedCompSpan(e,tiny,[]{});(void)other;});
                    check(!tiny.usage().reservedBytes,"Preparation refusal leaked tiny credit");
                }
                const auto name="output-"+std::to_string(rate)+"-"+std::to_string(anchors)+"-"+std::to_string(extension)+".f32le";pcm(root/name,output);
                rows.push_back({{"rate",rate},{"anchors",anchors},{"extension",extension},{"rawOrigin",rawOrigin},{"rawFrames",rawCount},{"outputFrames",d.outputFrames},{"prefix",d.prefixFrames},{"suffix",d.suffixFrames},{"envelope",points},{"candidate",replanned},{"cores",cores},{"processedGaps",report.processedGaps},{"reads",reads},{"writes",writes},{"polls",polls},{"pcm",name}});
                check(s==original,"Candidate changed source/project state");
            }
            check(!ledger.usage().reservedBytes,"Candidate retained resource credit");
        }
    }
    for(bool fractional:{false,true}){
        auto s=fixture(48000,fractional);bool refused=false;
        {auto e=envelope(s,fractional?3008:1,4544,ledger);try{auto p=prepareProtectedCompSpan(e,ledger,[]{});(void)p;}catch(const ProjectError &error){refused=error.code()==ErrorCode::UnsupportedSchema && std::string(error.what())=="Protected comp requires integer acoustic positions";}}
        check(refused && !ledger.usage().reservedBytes,"Fractional candidate accepted or leaked");refusals.push_back(fractional?"fractional-original-origin":"fractional-extended-origin");
    }
    {
        auto owned=[&]{auto s=fixture(48000,false,2);auto e=envelope(s,3008,1472,ledger);return prepareProtectedCompSpan(e,ledger,[]{});}();
        check(!owned.rawMap().empty() && owned.source().frames==98304,"Plan lost source after envelope destruction");
        auto moved=std::move(owned);bool refused=false;
        try{(void)owned.geometry();}catch(const ProjectError &error){refused=error.code()==ErrorCode::InvalidState;}
        check(refused,"Moved-from plan accepted");refusals.push_back("moved-from-plan");
        const auto report=renderProtectedCompSpan(moved,[](Frame,std::span<float> slab){std::fill(slab.begin(),slab.end(),2.5f);},[](auto){},ledger,[]{});
        check(report.writtenFrames==53632 && report.floatPeak>1,"Plan did not survive envelope destruction/move");
    }
    check(!ledger.usage().reservedBytes,"Independent plan leaked credit");
    {
        auto s=fixture(48000,false);auto &warp=*s.tracks[0].clips[0].stretch->warp;
        warp.protection.minimumNonunityGap=64;warp.markers[0].source={512,0,1};warp.markers[0].output={768,0,1};validate(s);
        bool refused=false;{auto e=envelope(s,0,0,ledger);try{auto plan=prepareProtectedCompSpan(e,ledger,[]{});(void)plan;}catch(const ProjectError &error){refused=error.code()==ErrorCode::UnsupportedSchema;}}
        check(refused && !ledger.usage().reservedBytes,"Short unqualified gap accepted or leaked");refusals.push_back("short-nonunity-edge-gap");
    }
    Json result{{"format","sc-comp-protected-span-tests-v1"},{"rows",rows},{"refusals",refusals},{"workerProtocolIntegrated",false},{"processor",compProtectedSpanProcessorId},{"qualityAdmission",false},{"nativeWindowsQualified",false}};
    std::ofstream f(root/"report.json");f<<result.dump(2)<<'\n';f.flush();check(bool(f),"Candidate report failed");std::cout<<Json{{"renders",rows.size()},{"refusals",refusals},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
