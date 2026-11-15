// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_pair_assembly.hpp>
#include <soundcurrent/comp_media_review.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include "media_io.hpp"
#include "windows_file_path.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
std::string bytes(const std::filesystem::path &p){std::ifstream f(fileSystemApiPath(p),std::ios::binary);check(bool(f),"Fixture input missing");return {(std::istreambuf_iterator<char>(f)),{}};}
void save(const std::filesystem::path &p,std::string_view text){std::ofstream f(fileSystemApiPath(p),std::ios::binary|std::ios::trunc);f.write(text.data(),std::streamsize(text.size()));check(bool(f),"Fixture write failed");}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid assembly accepted");}
SNDFILE *open(const std::filesystem::path &p,int mode,SF_INFO &info){
#ifdef _WIN32
    return sf_wchar_open(windowsFileApiPath(p).c_str(),mode,&info);
#else
    return sf_open(p.c_str(),mode,&info);
#endif
}
std::vector<float> decode(const std::filesystem::path &p){SF_INFO info{};auto f=open(p,SFM_READ,info);check(f && info.channels==2 && info.frames>0,"Invalid fixture wave");std::vector<float> v(std::size_t(info.frames)*2);const auto n=sf_readf_float(f,v.data(),info.frames);const auto closed=sf_close(f);check(n==info.frames && closed==0,"Fixture decode failed");return v;}
void wave(const std::filesystem::path &p,unsigned rate,const std::vector<float> &v){SF_INFO info{};info.samplerate=int(rate);info.channels=2;info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;auto f=open(p,SFM_WRITE,info);check(f!=nullptr,"Fixture wave create failed");const auto n=sf_writef_float(f,v.data(),sf_count_t(v.size()/2));const auto closed=sf_close(f);check(n==sf_count_t(v.size()/2) && closed==0,"Fixture wave write failed");}
std::vector<float> positioned(const std::vector<float> &v,const SourceFrameMap &map,Frame frames){
    PreparedPositionedResampling kernel(map,2);std::vector<float> out(std::size_t(frames)*2);
    // Different partitioning from production. This verifies map/offset and
    // chunk invariance, not an independent qualification of the FIR algorithm.
    for(Frame at=0;at<frames;at+=97){const auto n=std::uint32_t(std::min<Frame>(97,frames-at));const auto r=kernel.sourceRange(map,at,n,Frame(v.size()/2));
        kernel.process(map,at,Frame(v.size()/2),r.first,std::span(v).subspan(std::size_t(r.first)*2,r.frames*2),std::span(out).subspan(std::size_t(at)*2,std::size_t(n)*2));}return out;
}
bool exact(float a,float b){return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
const CompHandlePlanContext context{99,"assembly-test-owner"};
}
int main(int argc,char **argv){try{
    check(argc==2,"Expected assembly fixture mode");Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());ResourceLedger ledger(512*1024*1024,"Assembly test");auto s=ProjectStore(root).load();
    if(std::string_view(argv[1])=="nominal-prepare"){
        if(input.contains("pairKind")){
            const auto kind=input.at("pairKind").get<std::string>();auto &c=s.tracks[0].clips[1];
            check(kind=="protected" || kind=="uniform" || kind=="existing","Unknown protected pair fixture kind");
            if(kind=="uniform"){c.stretch->warp.reset();c.stretch->processor=std::string(stretchProcessorFor(c.stretch->settings));}
            if(kind=="existing"){c.stretch.reset();c.assetId=s.assets[0].id;c.sourceFrame=16384;c.sourceTiming={};}
            validate(s);ProjectStore(root).save(s);
        }
        Json requests=Json::array();const auto &t=s.tracks[0];
        for(const auto &clip:t.clips){if(!clip.stretch){requests.push_back(nullptr);continue;}auto plan=prepareClipStretch(s,t.id,clip.id,clip.stretch->settings,clip.stretch->context,clip.stretch->warp);
            const auto op=Id::generate();auto request=encodeStretchRenderRequest(plan,op,{},ledger);requests.push_back(Json::parse(request.bytes()));}
        save(root/"nominal-requests.json",requests.dump(2));std::cout<<requests.dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="nominal-ready"){
        const auto side=input.at("fragment").get<unsigned>();check(side<2,"Invalid nominal ready fragment");
        const auto requests=Json::parse(bytes(root/"nominal-requests.json"));const auto op=Id(requests.at(side).at("operation"));const auto &t=s.tracks[0];
        auto plan=prepareClipStretch(s,t.id,t.clips[side].id,t.clips[side].stretch->settings,t.clips[side].stretch->context,t.clips[side].stretch->warp);
        auto request=encodeStretchRenderRequest(plan,op,{},ledger);check(Json::parse(request.bytes())==requests.at(side),"Nominal request changed before start");
        verifyStretchRenderReady(input.at("ready").dump(),plan,op,ledger);
        save(root/("nominal-ready-"+std::to_string(side)+".json"),input.at("ready").dump());
        save(root/"media/derived"/op.str()/"start.request","start\n");std::cout<<Json{{"readyVerified",true}}.dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="nominal-adopt"){
        const auto requests=Json::parse(bytes(root/"nominal-requests.json"));Json accepted=Json::array();
        const auto before=s;EditHistory history(s);
        for(unsigned side=0;side<2;++side){if(requests.at(side).is_null()){accepted.push_back(nullptr);continue;}const auto &t=s.tracks[0];const auto op=Id(requests.at(side).at("operation"));
            auto plan=prepareClipStretch(s,t.id,t.clips[side].id,t.clips[side].stretch->settings,t.clips[side].stretch->context,t.clips[side].stretch->warp);auto request=encodeStretchRenderRequest(plan,op,{},ledger);
            check(Json::parse(request.bytes())==requests.at(side),"Original nominal request differs from canonical plan");
            verifyStretchRenderReady(bytes(root/("nominal-ready-"+std::to_string(side)+".json")),plan,op,ledger);
            auto verified=verifyOwnedClipStretch(root,plan,op,{},ledger);const auto previous=s;
            history.structural({verified.edit()});const auto applied=s;check(history.undo() && s==previous && history.redo() && s==applied,"Nominal adoption lost exact Undo/Redo");
            accepted.push_back({{"asset",verified.edit().rendered.id.str()},{"audioSha256",verified.edit().rendered.sha256},{"renderKey",verified.edit().value.renderKey}});
        }
        check(s!=before && s.tracks[0].takeLanes==before.tracks[0].takeLanes && s.assets[0]==before.assets[0],"Nominal adoption lost original raw/takes");
        ProjectStore(root).save(s);check(ProjectStore(root).load()==s,"Nominal adoption changed across reopen");
        save(root/"nominal-accepted.json",accepted.dump(2));std::cout<<accepted.dump()<<'\n';return 0;
    }
    if(std::string_view(argv[1])=="prepare"){
        if(input.at("physical").get<bool>()){
            auto &t=s.tracks[0];for(auto &c:t.clips){c.playbackRate={1,1};c.sourceTiming={0,1};if(c.stretch)c.lengthFrames=std::find_if(s.assets.begin(),s.assets.end(),[&](const auto &asset){return asset.id==c.assetId;})->frames;}
            t.clips[1].startFrame=t.clips[0].startFrame+t.clips[0].lengthFrames;validate(s);ProjectStore(root).save(s);
        }
        const auto &t=s.tracks[0];const auto join=t.clips[1].startFrame;
        const bool protectedMode=input.value("protected",false);
        const Frame before=protectedMode?3008:26;
        const Frame after=protectedMode?(t.clips[0].stretch->warp->markers.size()==1?4544:1472):26;
        auto plan=prepareCompProcessing(s,{t.id,t.clips[0].id,t.clips[1].id,join-before,join+after,ClipFadeCurve::EqualPower},context,8,ledger);
        const CompRecipeExecutor parent{"soundcurrent.comp-join-quintic-v1",std::string(64,'e')};
        auto recipe=encodeCompProcessingRecipe(s,context,plan,Id::generate(),parent,ledger);Json requests=Json::array();
        for(unsigned side=0;side<2;++side){
            const auto &processing=side?recipe.plan().incoming():recipe.plan().outgoing();
            if(processing.domain==CompProcessingDomain::ExistingMedia){requests.push_back(nullptr);continue;}
            const auto &c=t.clips[side];const auto binary=input.at("workerSha256").get<std::string>();
            auto r=protectedMode && c.stretch && c.stretch->warp?
                encodeProtectedCompSpanRequest(recipe,parent,side,Id::generate(),binary,ledger):
                encodeCompSpanRequest(recipe,parent,side,Id::generate(),binary,ledger);
            const auto file="request-"+std::to_string(side)+".json";save(root/file,r.bytes());requests.push_back({{"file",file},{"digest",r.digest()},{"operation",r.operation().str()}});}
        save(root/"requests.json",requests.dump());std::cout<<requests.dump()<<'\n';return 0;
    }
    const auto original=s;auto pair=loadReferencedCompPairSelection(root,s,context,s.compPairSelections.at(0).operation,ledger);
    if(std::string_view(argv[1])=="unready"){
        unsigned calls=0;refuses([&]{streamCompPairFragment(root,s,context,pair,0,[&](auto){++calls;},ledger);});check(!calls,"Incomplete pair emitted output");
        std::array<CompHandlePcmWrite,2> writes;for(unsigned side=0;side<2;++side)if(pair.children()[side])writes[side]=[&](auto){++calls;};
        refuses([&]{streamCompPairFragments(root,s,context,pair,writes,ledger);});check(!calls,"Incomplete batch emitted output");std::cout<<Json{{"incompletePairRefused",true}}.dump()<<'\n';return 0;
    }
    check(std::string_view(argv[1])=="verify","Unknown fixture mode");
    const auto baselineCredit=ledger.usage().reservedBytes;std::array<std::optional<Asset>,2> outputs;Json sides=Json::array();
    std::filesystem::create_directories(root/"media/comp-results"/pair.recipe().operation().str());
    for(unsigned side=0;side<2;++side){
        if(!pair.children()[side]){unsigned calls=0;refuses([&]{streamCompPairFragment(root,s,context,pair,side,[&](auto){++calls;},ledger);});check(!calls,"Existing media emitted new PCM");continue;}
        std::vector<float> actual;auto report=streamCompPairFragment(root,s,context,pair,side,[&](auto v){check(v.size()<=1024,"Assembly exceeded 512-frame sink slab");actual.insert(actual.end(),v.begin(),v.end());},ledger);
        const auto &e=(side?pair.recipe().plan().handles().incoming():pair.recipe().plan().handles().outgoing()).data();const auto &p=side?pair.recipe().plan().incoming():pair.recipe().plan().outgoing();
        for(unsigned n=0;n<2;++n)check(bool(report.nominals[n])==bool(s.tracks[0].clips[n].stretch),"Assembly lost original nominal presence");
        check(report.nominals[side] && report.nominals[side]->id==e.base.id,"Assembly lost original nominal binding");
        check(report.context==context && report.recipeDigest==pair.recipe().digest() && report.selectionDigest==pair.digest() && report.requestDigest==pair.children()[side]->digest(),"Assembly lost identity");
        check(report.domain==p.domain && report.pcm.writtenFrames==p.pcm.prefixFrames+p.pcm.currentFrames+p.pcm.suffixFrames,"Assembly lost geometry");
        auto old=decode(root/utf8Path(e.base.relativePath)),candidate=decode(root/utf8Path(report.span.relativePath));
        if(p.domain==CompProcessingDomain::ProjectAfterSrc){old=positioned(old,clipSourceMap(e.expected,e.base.sampleRate,e.projectRate),e.expected.lengthFrames);candidate=positioned(candidate,SourceFrameMap(e.base.sampleRate,e.projectRate,e.clipOutputOrigin,e.expected.playbackRate),e.projectEnd-e.projectFirst);}
        std::uint64_t exactSamples=0,blendedSamples=0;double maxError=0;
        for(Frame f=0;f<report.pcm.writtenFrames;++f){const auto inner=f-p.pcm.prefixFrames;
            for(unsigned ch=0;ch<2;++ch){const auto ix=std::size_t(f)*2+ch;float expected=candidate[ix];bool blend=false;
                if(inner>=0 && inner<p.pcm.currentFrames){expected=old[std::size_t(inner)*2+ch];std::optional<long double> oldWeight;
                    const auto weight=[](Frame offset,Frame width){const auto x=static_cast<long double>(offset+1)/static_cast<long double>(width+1);return 6*std::pow(x,5)-15*std::pow(x,4)+10*std::pow(x,3);};
                    if(p.blends.prefix && inner<p.blends.prefix->frames)oldWeight=weight(inner,p.blends.prefix->frames);
                    if(p.blends.suffix && inner>=p.pcm.currentFrames-p.blends.suffix->frames)oldWeight=1-weight(inner-(p.pcm.currentFrames-p.blends.suffix->frames),p.blends.suffix->frames);
                    if(oldWeight){blend=true;expected=float(*oldWeight*expected+(1-*oldWeight)*candidate[ix]);}
                }
                if(blend){++blendedSamples;maxError=std::max(maxError,std::abs(double(expected)-actual[ix]));check(std::abs(double(expected)-actual[ix])<=3e-7*std::max(1.,std::abs(double(expected))),"Independent blend curve differs");}
                else{++exactSamples;check(exact(expected,actual[ix]),"Unblended PCM changed");}
            }
        }
        check(blendedSamples && exactSamples && report.pcm.floatPeak>1,"Assembly lost coverage or float headroom");
        Asset out=e.base;out.id=Id::generate();out.nativeEvidence.clear();out.sampleRate=report.sampleRate;out.frames=report.pcm.writtenFrames;out.relativePath="media/comp-results/"+pair.recipe().operation().str()+"/"+out.id.str()+".wav";
        wave(root/utf8Path(out.relativePath),out.sampleRate,actual);out.sha256=hashMediaFile(root/utf8Path(out.relativePath));outputs[side]=out;
        sides.push_back({{"fragment",side},{"domain",unsigned(p.domain)},{"frames",out.frames},{"sha256",out.sha256},{"spanSha256",report.span.sha256},{"exactSamples",exactSamples},{"blendedSamples",blendedSamples},{"curveMaximumError",maxError},{"peakLinear",report.pcm.floatPeak}});
    }
    // Batched recomputation must preserve both complete legacy streams exactly.
    {
        std::array<std::vector<float>,2> actual;std::array<CompHandlePcmWrite,2> writes;
        unsigned lastSide=0;bool emitted=false;
        for(unsigned side=0;side<2;++side)if(outputs[side])writes[side]=[&,side](auto v){
            check(!emitted || side>=lastSide,"Batch interleaved output sides");lastSide=side;emitted=true;
            actual[side].insert(actual[side].end(),v.begin(),v.end());
        };
        auto reports=streamCompPairFragments(root,s,context,pair,writes,ledger);
        for(unsigned side=0;side<2;++side){
            check(bool(reports[side])==bool(outputs[side]),"Batch changed selected sides");if(!outputs[side])continue;
            const auto expected=decode(root/utf8Path(outputs[side]->relativePath));
            check(expected.size()==actual[side].size(),"Batch output extent changed");
            for(std::size_t n=0;n<expected.size();++n)check(exact(expected[n],actual[side][n]),"Batch PCM differs from single-fragment stream");
            check(reports[side]->pcm.writtenFrames==outputs[side]->frames && reports[side]->requestDigest==pair.children()[side]->digest(),"Batch report identity changed");
        }
    }
    check(ledger.usage().reservedBytes==baselineCredit,"Batch leaked payload credit");
    unsigned batchCalls=0;std::array<CompHandlePcmWrite,2> batchWrites;
    for(unsigned side=0;side<2;++side)if(outputs[side])batchWrites[side]=[&](auto){++batchCalls;};
    refuses([&]{streamCompPairFragments(root,s,context,pair,{},ledger);});
    auto batchLimit=CompPairAssemblyLimits{};batchLimit.maximumPcmBytes=64;
    refuses([&]{streamCompPairFragments(root,s,context,pair,batchWrites,ledger,batchLimit);});check(!batchCalls,"Batch preflight exposed samples");
    std::stop_source batchStop;unsigned firstSide=outputs[0]?0:1,otherCalls=0;
    auto canceledWrites=batchWrites;canceledWrites[firstSide]=[&](auto){batchStop.request_stop();};
    if(outputs[1] && firstSide==0)canceledWrites[1]=[&](auto){++otherCalls;};
    refuses([&]{streamCompPairFragments(root,s,context,pair,canceledWrites,ledger,{},batchStop.get_token());});check(!otherCalls,"Batch cancellation reached sibling output");
    const auto batchMarker=root/"media/comp-spans"/pair.children()[pair.children()[1]?1:0]->operation().str()/"complete.json";
    const auto batchMarkerBytes=bytes(batchMarker);bool batchAltered=false;auto alteredWrites=batchWrites;
    alteredWrites[firstSide]=[&](auto){if(!batchAltered){batchAltered=true;save(batchMarker,"{}");}};
    refuses([&]{streamCompPairFragments(root,s,context,pair,alteredWrites,ledger);});save(batchMarker,batchMarkerBytes);
    check(batchAltered && ledger.usage().reservedBytes==baselineCredit,"Late batch mutation missing or leaked credit");
    std::uint64_t physical=0,project=0;{auto review=reviewCompMedia(root,s,context,pair.recipe(),outputs,ledger);physical=review.counts().physicalSamplesCompared;project=review.counts().projectSamplesCompared;}
    unsigned calls=0;const CompHandlePcmWrite count=[&](auto){++calls;};
    auto stale=context;++stale.projectEpoch;refuses([&]{streamCompPairFragment(root,s,stale,pair,0,count,ledger);});
    auto changed=s;changed.tracks[0].clips[0].processing.gainDb=1;refuses([&]{streamCompPairFragment(root,changed,context,pair,0,count,ledger);});
    auto missing=s;missing.compPairSelections.clear();refuses([&]{streamCompPairFragment(root,missing,context,pair,0,count,ledger);});
    auto low=CompPairAssemblyLimits{};low.maximumPcmBytes=64;refuses([&]{streamCompPairFragment(root,s,context,pair,0,count,ledger,low);});
    ResourceLedger tiny(1,"Tiny assembly");refuses([&]{streamCompPairFragment(root,s,context,pair,0,count,tiny);});check(!tiny.usage().reservedBytes,"Failed assembly leaked tiny credit");
    refuses([&]{streamCompPairFragment(root,s,context,pair,2,count,ledger);});
    std::stop_source pre;pre.request_stop();refuses([&]{streamCompPairFragment(root,s,context,pair,0,count,ledger,{},pre.get_token());});check(!calls,"Preflight refusal emitted PCM");
    std::stop_source during;refuses([&]{streamCompPairFragment(root,s,context,pair,0,[&](auto){++calls;during.request_stop();},ledger,{},during.get_token());});check(calls==1,"Cancellation did not stop next output slab");
    refuses([&]{streamCompPairFragment(root,s,context,pair,0,[](auto){throw ProjectError(ErrorCode::Io,"Owned fixture sink failure");},ledger);});
    // Real completion corruption between the first output slab and final check.
    const auto complete=root/"media/comp-spans"/pair.children()[pair.children()[1]?1:0]->operation().str()/"complete.json";const auto before=bytes(complete);bool altered=false;
    refuses([&]{streamCompPairFragment(root,s,context,pair,0,[&](auto){if(!altered){altered=true;save(complete,"{}");}},ledger);});save(complete,before);check(altered,"Late marker refusal unexercised");
    const auto &nominal=s.assets.at(std::size_t(std::find_if(s.assets.begin(),s.assets.end(),[&](const auto &asset){return asset.id==s.tracks[0].clips[0].assetId;})-s.assets.begin()));
    const auto nominalMarker=root/"media/derived"/nominal.id.str()/"complete.json";const auto nominalBytes=bytes(nominalMarker);
    {media_io::JobLease exclusive(nominalMarker.parent_path(),false,true);check(exclusive.status()==media_io::LeaseStatus::Held,"Original lease fixture failed");calls=0;
        refuses([&]{streamCompPairFragment(root,s,context,pair,0,count,ledger);});check(!calls,"Busy original lease emitted PCM");}
    save(nominalMarker,"{}");calls=0;refuses([&]{streamCompPairFragment(root,s,context,pair,0,count,ledger);});check(!calls,"Unproven nominal emitted PCM");save(nominalMarker,nominalBytes);
    bool nominalAltered=false;refuses([&]{streamCompPairFragment(root,s,context,pair,0,[&](auto){if(!nominalAltered){nominalAltered=true;save(nominalMarker,"{}");}},ledger);});save(nominalMarker,nominalBytes);check(nominalAltered,"Late nominal receipt mutation unexercised");
    auto wrongPolicy=CompPairAssemblyLimits{};wrongPolicy.nominalPolicies[0].memoryBytes=128*1024*1024;calls=0;
    refuses([&]{streamCompPairFragment(root,s,context,pair,0,count,ledger,wrongPolicy);});check(!calls,"Nominal policy mismatch emitted PCM");
    check(s==original && ProjectStore(root).load()==original,"Assembly mutated canonical state");check(ledger.usage().reservedBytes==baselineCredit,"Assembly leaked payload credit");
    const Json result{{"checks",checks},{"sides",sides},{"physicalSamplesCompared",physical},{"projectSamplesCompared",project},{"ownedNominalWorkflowQualified",true},{"nominalBinaryBoundByProtocol",false},{"firQualityQualified",false},{"qualityAdmission",false},{"modelAdoptionChanged",false},{"postSrcLiveOverlayImplemented",false}};
    save(root/"assembly-report.json",result.dump(2));std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
