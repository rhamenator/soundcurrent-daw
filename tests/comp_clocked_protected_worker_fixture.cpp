// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_protocol.hpp>
#include <soundcurrent/comp_span_history.hpp>
#include <soundcurrent/media_cache.hpp>
#include "windows_file_path.hpp"
#include <bit>
#include <algorithm>
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <numbers>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
Json read(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);Json j;f>>j;check(bool(f),"Fixture JSON missing");return j;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view text){std::ofstream f(fileSystemApiPath(p),std::ios::binary|std::ios::trunc);f.write(text.data(),std::streamsize(text.size()));check(bool(f),"Fixture write failed");}
void wave(const std::filesystem::path &p,unsigned rate,const std::vector<float> &values){SF_INFO info{};info.samplerate=int(rate);info.channels=2;info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;
#ifdef _WIN32
    auto file=sf_wchar_open(p.c_str(),SFM_WRITE,&info);
#else
    auto file=sf_open(p.c_str(),SFM_WRITE,&info);
#endif
    check(file!=nullptr,"Fixture RF64 create failed");const auto frames=sf_count_t(values.size()/2);
    check(sf_writef_float(file,values.data(),frames)==frames,"Fixture RF64 write failed");check(sf_close(file)==0,"Fixture RF64 close failed");}
CompSpanWorkerPolicy policy(){CompSpanWorkerPolicy p;p.maximumSourceBytes=64*1024*1024;p.maximumOutputBytes=64*1024*1024;p.deadlineMilliseconds=10000;return p;}
}
int main(int argc,char **argv){try{check(argc==2,"Expected fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(384*1024*1024,"Protected span fixture");
    if(std::string_view(argv[1])=="prepare" || std::string_view(argv[1])=="prepare-history"){
        const bool history=std::string_view(argv[1])=="prepare-history";
        Session s;
        if(!history){
        const auto rate=input.at("rate").get<unsigned>();const unsigned anchors=2;
        std::filesystem::create_directories(root/"media"/"comp-spans");
        std::vector<float> values(196608*2);for(Frame f=0;f<196608;++f)for(unsigned ch=0;ch<2;++ch){const double t=double(f)-3*ch;double x=.07*std::sin(2*std::numbers::pi*997*t/rate);
            for(const double event:{t-49152,t-81920}){if(event>=0 && event<1024)x+=2.5*std::exp(-event/192)*std::cos(2*std::numbers::pi*1703*event/rate);}
            values[std::size_t(f)*2+ch]=float(ch?-.5*x:x);}
        wave(root/"media/raw.wav",rate,values);
        s=makeOneTrackSession("Clocked protected worker","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Stereo,2};t.eq.bands.clear();
        Asset raw;raw.layout=t.layout;raw.sampleRate=rate;raw.frames=196608;raw.relativePath="media/raw.wav";raw.sha256=hashMediaFile(root/"media/raw.wav");
        ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={32768,1,7};a.sourceFrames=65536;a.settings={3,2,0,true};
        WarpSettings warp;warp.protection={256,512,64,2048};warp.markers.push_back({Id("00000000-0000-0000-0000-000000000001"),{16384,1,3},{24576,1,5}});
        if(anchors==2)warp.markers.push_back({Id("00000000-0000-0000-0000-000000000002"),{49152,2,7},{73728,2,11}});
        a.warp=warp;a.processor=std::string(stretchProcessorFor(a.settings,a.context,a.warp));a.renderKey=std::string(64,'a');
        std::vector<float> nominal;
        {ProtectedWarpPlan plan({a.sourceOrigin,rate,raw.frames,65536,98304,{}, {65536,0,1}},warp.markers,warp.protection,ledger);
         PreparedClockedProtectedWarp sampling(plan,SourceFrameMap::processingGrid({}, {98304,0,1},98304),98304,2,ledger,[]{});
         renderClockedProtectedWarp(sampling,[&](Frame at,std::span<float> block){std::copy_n(values.begin()+at*2,block.size(),block.begin());},[&](auto block){nominal.insert(nominal.end(),block.begin(),block.end());},ledger,[]{});}
        wave(root/"media/current.wav",rate,nominal);
        Asset base=raw;base.id=Id::generate();base.relativePath="media/current.wav";base.frames=98304;base.sha256=hashMediaFile(root/"media/current.wav");s.assets={raw,base};
        TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=raw.frames*48000/rate;lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
        Clip c;c.assetId=base.id;c.startFrame=10000;c.lengthFrames=base.frames*48000/rate;c.stretch=a;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};
        auto second=c;second.id=Id::generate();second.startFrame=c.startFrame+c.lengthFrames;t.clips={c,second};validate(s);ProjectStore(root).save(s);
        }
        else {
            s=decodeProject(bytes(root/"adopted-project.json"));
            auto &t=s.tracks[0];check(t.clips.size()==2 && !s.compPlaybackSources.empty(),"Missing actual adopted history");
            for(auto &c:t.clips){c.sourceFrame+=20000;c.sourceTiming={1,7};c.lengthFrames=4000;c.playbackRate={3,2};}
            t.clips[1].startFrame=t.clips[0].startFrame+t.clips[0].lengthFrames;validate(s);
        }
        write(root/"clocked-project.json",encodeProject(s));
        const auto &t=s.tracks[0];const auto &c=t.clips[0],&second=t.clips[1];
        const PrepareCompTransition transition{t.id,c.id,second.id,second.startFrame-32,second.startFrame+32,ClipFadeCurve::EqualPower};
        auto plan=prepareCompProcessing(s,transition,{7,"parent-fixture"},8,ledger);
        const CompRecipeExecutor parent{"soundcurrent.comp-join-quintic-v1",std::string(64,'e')};auto recipe=encodeCompProcessingRecipe(s,{7,"parent-fixture"},plan,Id::generate(),parent,ledger);
        Json outputs=Json::array();for(unsigned side=0;side<2;++side){auto r=encodeClockedProtectedCompSpanRequest(recipe,parent,side,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);
            check(r.isProtected() && r.requiresHistory()==history && r.isClockedProtected() && r.protocol()==compClockedProtectedSpanProtocol && r.processor()==compClockedProtectedSpanProcessorId,"Protected protocol identity lost");
            const auto document=Json::parse(r.bytes());const auto intent=Json::parse(compSpanIntent(r,policy(),ledger));check(document.at("clock")==intent.at("clock") && document.at("protected")==intent.at("protected"),"Protected binding differs between request and readiness");
            bool refused=false;try{(void)encodeCompSpanRequest(recipe,parent,side,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);}catch(const ProjectError &){refused=true;}
            check(refused,"Legacy encoder silently selected protected DSP");
            const auto name="clocked-request-"+std::to_string(side)+".json";write(root/name,r.bytes());outputs.push_back({{"file",name},{"digest",r.digest()},{"operation",r.operation().str()},{"outputFrames",r.outputFrames()}});
            // Same piecewise timing but a different halo or semantic owner must
            // change the protected binding, independent of outer request hash.
            for(unsigned mutation=0;!history && mutation<2;++mutation){auto changed=s;for(auto &clip:changed.tracks[0].clips){if(mutation==0)clip.stretch->warp->protection.halo=32;else clip.stretch->warp->markers[0].id=Id("00000000-0000-0000-0000-000000000009");}
                auto cp=prepareCompProcessing(changed,transition,{7,"parent-fixture"},8,ledger);auto cr=encodeCompProcessingRecipe(changed,{7,"parent-fixture"},cp,Id::generate(),parent,ledger);
                auto request=encodeClockedProtectedCompSpanRequest(cr,parent,side,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);
                check(Json::parse(request.bytes()).at("protected").at("geometrySha256")!=document.at("protected").at("geometrySha256"),"Protected halo/owner identity was not bound");}
        }
        if(history)for(auto &row:outputs)row["historicalCompletion"]="media/comp-assemblies/"+s.compPlaybackSources.front().assembly.operation.str()+"/complete.json";
        write(root/"clocked-requests.json",outputs.dump());std::cout<<outputs.dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="active"){
        const auto outputs=read(root/"clocked-requests.json");const auto side=input.at("fragment").get<unsigned>();
        auto r=decodeCompSpanRequest(bytes(root/outputs[side].at("file").get<std::string>()),outputs[side].at("digest").get<std::string>(),{7,"parent-fixture"},input.at("workerSha256").get<std::string>(),ledger);
        bool refused=false;try{(void)verifyOwnedCompSpan(root,r,policy(),ledger);}catch(const ProjectError &){refused=true;}
        check(refused,"Active child admitted a completion");std::cout<<Json{{"activeRejected",true}}.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="verify","Unknown fixture mode");const auto outputs=read(root/"clocked-requests.json");auto s=decodeProject(bytes(root/"clocked-project.json"));const auto original=s;
    std::uint64_t compared=0,protectedSamples=0;
    for(unsigned side=0;side<2;++side){auto r=decodeCompSpanRequest(bytes(root/outputs[side].at("file").get<std::string>()),outputs[side].at("digest").get<std::string>(),{7,"parent-fixture"},input.at("workerSha256").get<std::string>(),ledger);
        verifyCompProcessingCurrent(s,{7,"parent-fixture"},r.recipe().plan());verifyCompSpanReady(read(root/("ready-"+std::to_string(side)+".json")).dump(),r,policy(),ledger);
        verifyCompSpanHistory(root,r,policy(),ledger,[]{});
        auto verified=verifyOwnedCompSpan(root,r,policy(),ledger);check(verified.asset().frames==r.outputFrames() && verified.requestDigest()==r.digest(),"Owned span output lost identity");
        {
            const std::array<Asset,2> assets{r.source(),verified.asset()};MediaCacheConfig config;config.maximumOpenFiles=2;config.pageFrames=512;config.cacheBudgetBytes=1024*1024;config.registryBudgetBytes=256*1024;config.resources=ledger;
            MediaReadCache cache(root,assets,r.source().sampleRate,config);Frame at=0;std::vector<float> actual(1024);
            renderRequestedCompSpan(r,[&](Frame first,std::span<float> slab){cache.read(0,first,slab);},[&](std::span<const float> expected){auto view=std::span(actual).first(expected.size());cache.read(1,at,view);
                for(std::size_t i=0;i<expected.size();++i){check(std::bit_cast<std::uint32_t>(expected[i])==std::bit_cast<std::uint32_t>(view[i]),"Worker PCM differs from full core recomputation");}
                at+=Frame(expected.size()/2);compared+=expected.size();},ledger,[]{});
            check(at==r.outputFrames(),"Worker recomputation extent differs");
            const auto &clock=r.clockedProtectedSpan().sampling().clock();
            check(scaleSourcePosition({1,0,1},clock.numerator(),clock.denominator())==SourcePosition{1,0,1},"Initial worker lost raw sample clock");
            protectedSamples+=r.clockedProtectedSpan().geometry().spans().size();
        }
        const auto refusal=[&](auto action){try{action();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid owned span accepted");};
        std::stop_source canceled;canceled.request_stop();refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger,canceled.get_token());});
        const auto path=root/utf8Path(verified.asset().relativePath);const auto audio=bytes(path);auto corrupt=audio;corrupt.back()^=1;write(path,corrupt);refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(path,audio);
        const auto marker=path.parent_path()/"complete.json";const auto complete=bytes(marker);auto altered=Json::parse(complete);altered["fragment"]=1-side;write(marker,altered.dump());refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(marker,complete);
        const auto rawPath=root/"media/raw.wav";const auto raw=bytes(rawPath);auto bad=raw;bad.back()^=1;write(rawPath,bad);refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(rawPath,raw);
        auto changed=s;changed.tracks[0].clips[side].processing.gainDb=1;refusal([&]{verifyCompProcessingCurrent(changed,{7,"parent-fixture"},r.recipe().plan());});
        check(s==original,"Owned verification modified project");
    }
    check(ledger.usage().reservedBytes==0,"Owned request/verification leaked payload");std::cout<<Json{{"checks",checks},{"completedSpans",2},{"comparedSamples",compared},{"semanticAnchorsVerified",protectedSamples},{"workerProtocolIntegrated",true},{"clockedProtected",true},{"modelAdoptionChanged",false},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
