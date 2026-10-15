// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_span_protocol.hpp>
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
Json read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);Json j;f>>j;check(bool(f),"Fixture JSON missing");return j;}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
void write(const std::filesystem::path &p,std::string_view text){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(text.data(),std::streamsize(text.size()));check(bool(f),"Fixture write failed");}
void wave(const std::filesystem::path &p,unsigned rate,Frame frames){SF_INFO info{};info.samplerate=int(rate);info.channels=2;info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;
#ifdef _WIN32
    auto file=sf_wchar_open(p.c_str(),SFM_WRITE,&info);
#else
    auto file=sf_open(p.c_str(),SFM_WRITE,&info);
#endif
    check(file!=nullptr,"Fixture RF64 create failed");std::vector<float> values(std::size_t(frames)*2);for(Frame f=0;f<frames;++f)for(unsigned ch=0;ch<2;++ch)values[std::size_t(f)*2+ch]=float(2.5*std::sin(2*std::numbers::pi*(ch?1703:997)*double(f)/rate));
    check(sf_writef_float(file,values.data(),frames)==frames,"Fixture RF64 write failed");check(sf_close(file)==0,"Fixture RF64 close failed");}
CompSpanWorkerPolicy policy(){CompSpanWorkerPolicy p;p.maximumSourceBytes=64*1024*1024;p.maximumOutputBytes=64*1024*1024;p.deadlineMilliseconds=10000;return p;}
}
int main(int argc,char **argv){try{check(argc==2,"Expected fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(384*1024*1024,"Span fixture");
    if(std::string_view(argv[1])=="prepare"){
        const auto rate=input.at("rate").get<unsigned>();std::filesystem::create_directories(root/"media"/"comp-spans");wave(root/"media/raw.wav",rate,98304);wave(root/"media/current.wav",rate,24576);
        auto s=makeOneTrackSession("Owned span test","Audio",48000);auto &t=s.tracks[0];t.layout={LayoutKind::Stereo,2};t.eq.bands.clear();
        Asset raw;raw.layout=t.layout;raw.sampleRate=rate;raw.frames=98304;raw.relativePath="media/raw.wav";raw.sha256=hashMediaFile(root/"media/raw.wav");
        Asset base=raw;base.id=Id::generate();base.relativePath="media/current.wav";base.frames=24576;base.sha256=hashMediaFile(root/"media/current.wav");s.assets={raw,base};
        TakeLane lane;Clip take;take.assetId=raw.id;take.lengthFrames=SourceFrameMap(rate,48000).projectFramesForSource(raw.frames);lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
        ClipStretchAnchor a;a.sourceAssetId=raw.id;a.sourceSha256=raw.sha256;a.sourceOrigin={32768,1,7};a.sourceFrames=16384;a.settings={3,2,input.at("pitch").get<int>(),false};a.processor=std::string(stretchProcessorFor(a.settings));a.renderKey=std::string(64,'a');
        Clip c;c.assetId=base.id;c.startFrame=100;c.stretch=a;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};c.playbackRate={3,2};c.sourceTiming={1,3};
        c.lengthFrames=clipSourceMap(c,rate,48000).projectFramesForSource(base.frames);while(clipSourceMap(c,rate,48000).at(c.lengthFrames-1).frame>=base.frames)--c.lengthFrames;
        auto second=c;second.id=Id::generate();second.startFrame=c.startFrame+c.lengthFrames;t.clips={c,second};validate(s);ProjectStore(root).save(s);
        auto plan=prepareCompProcessing(s,{t.id,c.id,second.id,second.startFrame-26,second.startFrame+26,ClipFadeCurve::EqualPower},{7,"parent-fixture"},8,ledger);
        const CompRecipeExecutor parent{"soundcurrent.comp-join-quintic-v1",std::string(64,'e')};auto recipe=encodeCompProcessingRecipe(s,{7,"parent-fixture"},plan,Id::generate(),parent,ledger);
        Json outputs=Json::array();for(unsigned side=0;side<2;++side){auto r=encodeCompSpanRequest(recipe,parent,side,Id::generate(),input.at("workerSha256").get<std::string>(),ledger);
            const auto name="request-"+std::to_string(side)+".json";write(root/name,r.bytes());outputs.push_back({{"file",name},{"digest",r.digest()},{"operation",r.operation().str()},{"outputFrames",r.span().outputFrames()}});}
        write(root/"requests.json",outputs.dump());std::cout<<outputs.dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="active"){
        const auto outputs=read(root/"requests.json");const auto side=input.at("fragment").get<unsigned>();
        auto r=decodeCompSpanRequest(bytes(root/outputs[side].at("file").get<std::string>()),outputs[side].at("digest").get<std::string>(),{7,"parent-fixture"},input.at("workerSha256").get<std::string>(),ledger);
        bool refused=false;try{(void)verifyOwnedCompSpan(root,r,policy(),ledger);}catch(const ProjectError &){refused=true;}
        check(refused,"Active child admitted a completion");std::cout<<Json{{"activeRejected",true}}.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="verify","Unknown fixture mode");const auto outputs=read(root/"requests.json");auto s=ProjectStore(root).load();const auto original=s;
    for(unsigned side=0;side<2;++side){auto r=decodeCompSpanRequest(bytes(root/outputs[side].at("file").get<std::string>()),outputs[side].at("digest").get<std::string>(),{7,"parent-fixture"},input.at("workerSha256").get<std::string>(),ledger);
        verifyCompProcessingCurrent(s,{7,"parent-fixture"},r.recipe().plan());verifyCompSpanReady(read(root/("ready-"+std::to_string(side)+".json")).dump(),r,policy(),ledger);
        auto verified=verifyOwnedCompSpan(root,r,policy(),ledger);check(verified.asset().frames==r.span().outputFrames() && verified.requestDigest()==r.digest(),"Owned span output lost identity");
        const auto refusal=[&](auto action){try{action();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid owned span accepted");};
        std::stop_source canceled;canceled.request_stop();refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger,canceled.get_token());});
        const auto path=root/utf8Path(verified.asset().relativePath);const auto audio=bytes(path);auto corrupt=audio;corrupt.back()^=1;write(path,corrupt);refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(path,audio);
        const auto marker=path.parent_path()/"complete.json";const auto complete=bytes(marker);auto altered=Json::parse(complete);altered["fragment"]=1-side;write(marker,altered.dump());refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(marker,complete);
        for(const char *metric:{"address-space","process-commit"}) {
            auto portable=Json::parse(complete);portable["memoryMetric"]=metric;write(marker,portable.dump());
            auto reviewed=verifyOwnedCompSpan(root,r,policy(),ledger);
            check(reviewed.asset()==verified.asset() && reviewed.requestDigest()==verified.requestDigest(),
                  "Producer memory metric changed portable span identity");
            portable["sampleSha256"]=std::string(64,'f');write(marker,portable.dump());
            refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});
        }
        for(const Json &metric:{Json("resident-set"),Json(""),Json(nullptr),Json(1),Json(true),Json::array(),Json::object()}) {
            auto bad=Json::parse(complete);bad["memoryMetric"]=metric;write(marker,bad.dump());
            refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});
        }
        auto missingMetric=Json::parse(complete);missingMetric.erase("memoryMetric");write(marker,missingMetric.dump());
        refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(marker,complete);
        const auto rawPath=root/"media/raw.wav";const auto raw=bytes(rawPath);auto bad=raw;bad.back()^=1;write(rawPath,bad);refusal([&]{verifyOwnedCompSpan(root,r,policy(),ledger);});write(rawPath,raw);
        auto changed=s;changed.tracks[0].clips[side].processing.gainDb=1;refusal([&]{verifyCompProcessingCurrent(changed,{7,"parent-fixture"},r.recipe().plan());});
        check(s==original,"Owned verification modified project");
    }
    check(ledger.usage().reservedBytes==0,"Owned request/verification leaked payload");std::cout<<Json{{"checks",checks},{"completedSpans",2},{"modelAdoptionChanged",false},{"qualityAdmission",false}}.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
