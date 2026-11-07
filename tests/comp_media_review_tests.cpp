// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_media_review.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0,physicalCases=0,projectCases=0;
void check(bool ok,const char *m){++checks;if(!ok)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid actual media review accepted");}
std::vector<float> pcm(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary|std::ios::ate);check(bool(f),"Owned PCM missing");const auto size=f.tellg();check(size>0 && size%4==0,"Owned PCM size differs");std::vector<float> v(std::size_t(size)/4);f.seekg(0);f.read(reinterpret_cast<char *>(v.data()),size);check(bool(f),"Owned PCM read failed");return v;}
void wave(const std::filesystem::path &p,std::span<const float> v,unsigned rate,int encoding=SF_FORMAT_FLOAT){
    SF_INFO info{};info.channels=2;info.samplerate=int(rate);info.format=SF_FORMAT_RF64|encoding;
#ifdef _WIN32
    auto file=sf_wchar_open(p.c_str(),SFM_WRITE,&info);
#else
    auto file=sf_open(p.c_str(),SFM_WRITE,&info);
#endif
    check(file!=nullptr,"Owned WAV create failed");const auto written=sf_writef_float(file,v.data(),sf_count_t(v.size()/2));const auto error=sf_error(file);const auto closed=sf_close(file);
    check(written==sf_count_t(v.size()/2) && error==SF_ERR_NO_ERROR && closed==0,"Owned WAV write failed");
}
std::vector<float> decode(const std::filesystem::path &p){SF_INFO info{};
#ifdef _WIN32
    auto file=sf_wchar_open(p.c_str(),SFM_READ,&info);
#else
    auto file=sf_open(p.c_str(),SFM_READ,&info);
#endif
    check(file && info.channels==2 && info.frames>0,"Owned WAV decode failed");std::vector<float> v(std::size_t(info.frames)*2);const auto got=sf_readf_float(file,v.data(),info.frames);const auto closed=sf_close(file);check(got==info.frames && closed==0,"Owned WAV decode incomplete");return v;}
CompHandlePcmRead read(const std::vector<float> &v){return [&v](Frame f,std::span<float> out){check(f>=0 && std::size_t(f)*2<=v.size() && out.size()<=v.size()-std::size_t(f)*2,"Fixture read out of range");std::copy_n(v.begin()+f*2,out.size(),out.begin());};}
std::vector<float> resample(const std::vector<float> &v,const SourceFrameMap &map,Frame frames){PreparedPositionedResampling k(map,2);std::vector<float> out(std::size_t(frames)*2);
    for(Frame at=0;at<frames;at+=127){const auto n=std::uint32_t(std::min<Frame>(127,frames-at));const auto r=k.sourceRange(map,at,n,Frame(v.size()/2));
        k.process(map,at,Frame(v.size()/2),r.first,std::span(v).subspan(std::size_t(r.first)*2,r.frames*2),std::span(out).subspan(std::size_t(at)*2,std::size_t(n)*2));}return out;}
}
int main(int argc,char **argv){try{
    static_assert(!std::is_copy_constructible_v<ReviewedCompMedia>);static_assert(!std::is_move_assignable_v<ReviewedCompMedia>);
    check(argc==3,"Supply owned PCM bank and new result root");check(std::endian::native==std::endian::little,"Owned bank needs little-endian floats");
    const auto bank=utf8Path(argv[1]),scope=utf8Path(argv[2]);check(std::filesystem::create_directory(scope),"Evidence scope already exists");
    std::ifstream reportFile(bank/"report.json");Json report;reportFile>>report;check(bool(reportFile),"Owned report missing");
    ResourceLedger ledger(192*1024*1024,"Actual comp file review");std::uint64_t physicalSamples=0,projectSamples=0;Json cases=Json::array();unsigned caseNumber=0;
    for(const auto &row:report.at("cases"))for(unsigned projectRate:{44100u,48000u,96000u}){
        const auto root=scope/utf8Path("project-é-日本-"+std::to_string(caseNumber));std::filesystem::create_directories(root/"media");
        auto s=makeOneTrackSession("Actual R3 bank review","Audio",projectRate);auto &t=s.tracks[0];t.layout={LayoutKind::Stereo,2};t.eq.bands.clear();
        auto rawPcm=pcm(bank/("raw-"+std::to_string(row.at("signal").get<unsigned>())+".pcm"));
        auto nominalPcm=pcm(bank/row.at("case").get<std::string>()/"nominal.pcm");
        const auto encodingCase=(caseNumber/3+caseNumber%3)%3;
        const int encoding=encodingCase==0?SF_FORMAT_FLOAT:encodingCase==1?SF_FORMAT_DOUBLE:SF_FORMAT_PCM_24;
        wave(root/"media/raw.wav",rawPcm,48000);wave(root/"media/current.wav",nominalPcm,48000,encoding);nominalPcm=decode(root/"media/current.wav");
        wave(root/"media/retained.wav",rawPcm,48000);
        Asset raw;raw.layout=t.layout;raw.relativePath="media/raw.wav";raw.frames=Frame(rawPcm.size()/2);raw.sha256=hashMediaFile(root/"media/raw.wav");
        const auto originalDirectory="media/original-"+raw.id.str()+"/";
        std::filesystem::create_directory(root/utf8Path(originalDirectory));
        raw.relativePath=originalDirectory+"take.wav";std::filesystem::rename(root/"media/raw.wav",root/utf8Path(raw.relativePath));
        // Deliberately synthetic, schema-shaped companion bytes. This test
        // verifies byte binding only, never native capture semantics.
        const std::string evidenceBytes="fixture-only: not a native recording receipt\n";
        for(const char *name:{"native-run.json","journal.json","packets.bin"}){const auto path=originalDirectory+name;
            {std::ofstream f(root/utf8Path(path),std::ios::binary);f<<evidenceBytes;}
            raw.nativeEvidence.push_back({path,hashMediaFile(root/utf8Path(path)),std::filesystem::file_size(root/utf8Path(path))});}
        Asset base=raw;base.nativeEvidence.clear();base.id=Id::generate();base.relativePath="media/current.wav";base.frames=Frame(nominalPcm.size()/2);base.sha256=hashMediaFile(root/"media/current.wav");
        Asset retained=raw;retained.nativeEvidence.clear();retained.id=Id::generate();retained.relativePath="media/retained.wav";retained.sha256=hashMediaFile(root/"media/retained.wav");
        ClipStretchAnchor takeAnchor;takeAnchor.sourceAssetId=raw.id;takeAnchor.sourceSha256=raw.sha256;takeAnchor.sourceFrames=raw.frames;takeAnchor.settings={1,1,0,false};takeAnchor.processor=std::string(unityStretchProcessorId);takeAnchor.renderKey=std::string(64,'f');
        TakeLane lane;Clip take;take.assetId=retained.id;take.stretch=takeAnchor;take.lengthFrames=SourceFrameMap(48000,projectRate).projectFramesForSource(raw.frames);lane.captureEndFrame=take.lengthFrames;lane.clips={take};t.takeLanes={lane};
        ClipStretchAnchor anchor;anchor.sourceAssetId=raw.id;anchor.sourceSha256=raw.sha256;anchor.sourceOrigin={32768,1,7};anchor.sourceFrames=16384;
        anchor.settings={row.at("timeNumerator"),row.at("timeDenominator"),row.at("pitchMilliCents"),false};anchor.processor=std::string(stretchProcessorFor(anchor.settings));anchor.renderKey=std::string(64,'a');
        Clip c;c.assetId=base.id;c.stretch=anchor;c.takeOrigin=ClipTakeOrigin{t.id,lane.id,take.id};c.startFrame=100;
        if(caseNumber%2)c.sourceTiming={1,3};
        if(caseNumber%4==0)c.playbackRate={3,2};
        auto map=clipSourceMap(c,48000,projectRate);c.lengthFrames=map.projectFramesForSource(base.frames);while(map.at(c.lengthFrames-1).frame>=base.frames)--c.lengthFrames;
        auto incoming=c;incoming.id=Id::generate();incoming.startFrame=c.startFrame+c.lengthFrames;t.clips={c,incoming};s.assets={raw,base,retained};validate(s);const auto original=s;
        const CompHandlePlanContext context{7,"owned-Unicode-review-"+std::to_string(caseNumber)};const auto boundary=incoming.startFrame;
        const auto plan=prepareCompProcessing(s,{t.id,c.id,incoming.id,boundary-26,boundary+26,ClipFadeCurve::EqualPower},context,8,ledger);
        const auto op=Id::generate();const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",std::string(64,'c')};
        auto recipe=encodeCompProcessingRecipe(s,context,plan,op,executor,ledger);std::filesystem::create_directories(root/"media"/"comp-results"/op.str());
        const std::array<const CompHandleEnvelope *,2> envelopes{&plan.handles().outgoing(),&plan.handles().incoming()};
        const std::array<const CompFragmentProcessing *,2> processing{&plan.outgoing(),&plan.incoming()};
        std::array<std::optional<Asset>,2> assets;std::array<std::vector<float>,2> results;
        for(unsigned side=0;side<2;++side){const auto &e=envelopes[side]->data();const auto &p=*processing[side];check(p.domain!=CompProcessingDomain::ExistingMedia,"Corpus unexpectedly needs no result");
            auto contextPcm=pcm(bank/row.at("case").get<std::string>()/(side?"prefix-context.pcm":"suffix-context.pcm"));
            // Candidate samples come from the frozen actual R3 bank. Their
            // alignment/curve quality is not certified by this scope review.
            std::vector<float> candidate(std::size_t(e.outputFrames)*2);for(std::size_t i=0;i<candidate.size();++i)candidate[i]=contextPcm[i%contextPcm.size()];
            std::vector<float> old=nominalPcm;
            if(p.domain==CompProcessingDomain::ProjectAfterSrc){++projectCases;old=resample(nominalPcm,clipSourceMap(e.expected,48000,projectRate),e.expected.lengthFrames);
                candidate=resample(candidate,SourceFrameMap(48000,projectRate,e.clipOutputOrigin,e.expected.playbackRate),e.projectEnd-e.projectFirst);
            }else ++physicalCases;
            blendCompHandlePcm(p.pcm,p.blends,read(old),read(candidate),[&](auto slab){results[side].insert(results[side].end(),slab.begin(),slab.end());},ledger,[]{});
            Asset a=base;a.nativeEvidence.clear();a.id=Id::generate();a.relativePath="media/comp-results/"+op.str()+"/"+a.id.str()+".wav";a.frames=Frame(results[side].size()/2);a.sampleRate=p.domain==CompProcessingDomain::PhysicalArtifact?48000:projectRate;
            wave(root/utf8Path(a.relativePath),results[side],a.sampleRate);a.sha256=hashMediaFile(root/utf8Path(a.relativePath));assets[side]=a;
        }
        {
            auto reviewed=reviewCompMedia(root,s,context,recipe,assets,ledger);physicalSamples+=reviewed.counts().physicalSamplesCompared;projectSamples+=reviewed.counts().projectSamplesCompared;
            check(reviewed.outputs()==assets && reviewed.recipeDigest()==recipe.digest() && reviewed.context()==context,"Review lost output/recipe binding");
        }
        if(caseNumber==0){auto rawSession=s;for(auto &clip:rawSession.tracks[0].clips){clip.assetId=raw.id;clip.stretch.reset();clip.sourceFrame=32768;clip.sourceTiming={0,1};}
            rawSession.tracks[0].takeLanes[0].clips[0].assetId=raw.id;rawSession.tracks[0].takeLanes[0].clips[0].stretch.reset();
            validate(rawSession);const CompHandlePlanContext rawContext{8,"existing-media"};const auto &track=rawSession.tracks[0];
            auto rawPlan=prepareCompProcessing(rawSession,{track.id,track.clips[0].id,track.clips[1].id,boundary-26,boundary+26,ClipFadeCurve::Linear},rawContext,8,ledger);
            auto rawRecipe=encodeCompProcessingRecipe(rawSession,rawContext,rawPlan,Id::generate(),executor,ledger);
            auto unchanged=reviewCompMedia(root,rawSession,rawContext,rawRecipe,{},ledger);check(!unchanged.outputs()[0] && !unchanged.outputs()[1],"Existing-media review created results");
            refuses([&]{reviewCompMedia(root,rawSession,rawContext,rawRecipe,assets,ledger);});
            ResourceLedger tiny(1,"Tiny review");refuses([&]{reviewCompMedia(root,s,context,recipe,assets,tiny);});check(!tiny.usage().reservedBytes,"Tiny review leaked credit");
        }
        auto stale=context;++stale.projectEpoch;refuses([&]{reviewCompMedia(root,s,stale,recipe,assets,ledger);});
        auto changed=s;changed.tracks[0].clips[1].processing.gainDb=1;refuses([&]{reviewCompMedia(root,changed,context,recipe,assets,ledger);});
        std::stop_source canceled;canceled.request_stop();refuses([&]{reviewCompMedia(root,s,context,recipe,assets,ledger,{},canceled.get_token());});
        auto missing=assets;missing[1].reset();refuses([&]{reviewCompMedia(root,s,context,recipe,missing,ledger);});
        auto wrong=assets;wrong[1]->relativePath=base.relativePath;refuses([&]{reviewCompMedia(root,s,context,recipe,wrong,ledger);});
        const auto path=root/utf8Path(assets[0]->relativePath);auto corrupt=results[0];const auto &p=plan.outgoing();const auto index=std::size_t(p.pcm.prefixFrames)*2;corrupt[index]+=0.125f;
        wave(path,corrupt,assets[0]->sampleRate);auto matched=assets;matched[0]->sha256=hashMediaFile(path);
        refuses([&]{reviewCompMedia(root,s,context,recipe,matched,ledger);});
        wave(path,results[0],assets[0]->sampleRate);check(hashMediaFile(path)==assets[0]->sha256,"Restored output bytes differ");
        auto low=CompMediaReviewLimits{};low.maximumResultBytes=8;refuses([&]{reviewCompMedia(root,s,context,recipe,assets,ledger,low);});
        // Changing a separately processed retained source must fail even when
        // raw/current files and output PCM are intact.
        auto retainedBad=rawPcm;retainedBad.back()+=.25f;wave(root/"media/retained.wav",retainedBad,48000);
        refuses([&]{reviewCompMedia(root,s,context,recipe,assets,ledger);});wave(root/"media/retained.wav",rawPcm,48000);
        {std::ofstream altered(root/utf8Path(raw.nativeEvidence[0].relativePath),std::ios::binary|std::ios::trunc);auto changedEvidence=evidenceBytes;changedEvidence.front()='F';altered<<changedEvidence;}
        refuses([&]{reviewCompMedia(root,s,context,recipe,assets,ledger);});
        {std::ofstream restored(root/utf8Path(raw.nativeEvidence[0].relativePath),std::ios::binary|std::ios::trunc);restored<<evidenceBytes;}
        check(hashMediaFile(root/"media/retained.wav")==retained.sha256 && s==original,"Refused review changed source state");
        {std::ofstream saved(root/"project.json",std::ios::binary);saved<<encodeProject(s);check(bool(saved),"Project evidence save failed");}
        {std::ofstream saved(root/"recipe.json",std::ios::binary);saved<<recipe.bytes();check(bool(saved),"Recipe evidence save failed");}
        cases.push_back({{"case",caseNumber++},{"sourceBankCase",row.at("case")},{"projectRate",projectRate},{"currentEncoding",encoding},{"recipeDigest",recipe.digest()},{"outputs",{assets[0]->sha256,assets[1]->sha256}}});
    }
    check(physicalCases && projectCases && physicalSamples && projectSamples,"Actual file corpus missed review domains");
    check(!ledger.usage().reservedBytes,"File review/recipe owners retained credit");
    const Json result{{"format","sc-comp-media-review-tests-v1"},{"checks",checks},{"cases",cases},{"physicalFragments",physicalCases},{"postSrcFragments",projectCases},{"physicalSamplesCompared",physicalSamples},{"projectSamplesCompared",projectSamples},{"freshR3Renders",0},{"sourceBankR3Renders",36},{"existingFragmentsReviewed",2},{"nativeEvidenceSemanticQualification",false},{"workerCompletionQualified",false},{"producerAlignmentQualified",false},{"qualityAdmission",false},{"modelAdoptionChanged",false}};
    std::ofstream out(scope/"report.json");out<<result.dump(2)<<'\n';check(bool(out),"Review report write failed");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
