// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/comp_processing_recipe.hpp>
#include <soundcurrent/comp_span_render.hpp>
#include <soundcurrent/comp_playback_source.hpp>
#include <soundcurrent/comp_pair_recovery.hpp>
#include <soundcurrent/media_cache.hpp>
#include <complex>
#include <numbers>
#include <cmath>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
unsigned plannedRecipes=0,legacyRecipes=0;
Json clockRenders=Json::array();
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid regeneration source accepted");}
std::string bytes(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);check(bool(f),"Missing actual adopted source");return {(std::istreambuf_iterator<char>(f)),{}};}
const CompHandlePlanContext context{73,"regeneration-owner/é-日本"};
static_assert(!std::is_copy_constructible_v<CompRegenerationSource> && !std::is_move_assignable_v<CompRegenerationSource>);
// Metadata-only additional generations exercise the already versioned lineage
// representation. These declarations are not advertised as rendered/verified PCM.
Session layer(Session s,bool post,bool cropped,ResourceLedger ledger){
    auto &c=s.tracks[0].clips[0];
    if(cropped){c.lengthFrames/=2;c.sourceTiming={1,7};c.playbackRate={3,2};}
    validate(s);auto resolved=resolveCompRegenerationSource(s,s.tracks[0].id,c.id,context,ledger);
    const auto &r=resolved.data();CompPlaybackSourceData d;
    d.original=d.playback=c;d.raw=r.raw;d.base=r.current;d.rendered=r.current;d.rendered.id=Id::generate();d.rendered.nativeEvidence.clear();d.rendered.sha256=std::string(64,'f');
    d.projectRate=s.sampleRate;d.rawMap=r.rawMap;
    d.domain=post?CompProcessingDomain::ProjectAfterSrc:CompProcessingDomain::PhysicalArtifact;
    if(post){const auto map=clipSourceMap(c,r.current.sampleRate,s.sampleRate);d.expandedOrigin=map.at(0);d.expandedNumerator=map.numerator();d.expandedDenominator=map.denominator();
        d.rendered.sampleRate=s.sampleRate;d.rendered.frames=c.lengthFrames;d.playback.sourceFrame=0;d.playback.sourceTiming={};d.playback.playbackRate={};}
    const auto selection=Id::generate();const CompAssemblyReference assembly{Id::generate(),selection,std::string(64,'e'),{d.rendered.id,{}}};
    d.rendered.relativePath="media/comp-results/"+selection.str()+"/"+d.rendered.id.str()+".wav";d.playback.assetId=d.rendered.id;
    s.compPairSelections.push_back({selection,std::string(64,'d')});s.assets.push_back(d.rendered);s.compPlaybackSources.push_back({assembly,0,std::string(64,'d'),d});c=d.playback;validate(s);return s;
}
struct PitchObservation {double fundamental=0,globalPeak=0,fundamentalRelativeAmplitude=0;};
PitchObservation frequency(const std::vector<float> &v,unsigned ch,unsigned rate,double wanted){
    const auto first=v.size()/2/4,last=3*(v.size()/2)/4,count=last-first;std::size_t n=1;while(n<count*8)n*=2;
    std::vector<std::complex<double>> spectrum(n);
    for(std::size_t i=0;i<count;++i)spectrum[i]=double(v[(first+i)*2+ch])*(.5-.5*std::cos(2*std::numbers::pi*double(i)/double(count-1)));
    for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(spectrum[i],spectrum[j]);}
    for(std::size_t size=2;size<=n;size*=2){const auto unit=std::polar(1.,-2*std::numbers::pi/double(size));
        for(std::size_t at=0;at<n;at+=size){std::complex<double> weight=1;for(std::size_t k=0;k<size/2;++k){const auto even=spectrum[at+k],odd=spectrum[at+k+size/2]*weight;spectrum[at+k]=even+odd;spectrum[at+k+size/2]=even-odd;weight*=unit;}}}
    const auto peak=[&](double low,double high){const auto begin=std::max<std::size_t>(1,std::size_t(std::ceil(low*double(n)/rate))),end=std::min(n/2,std::size_t(std::floor(high*double(n)/rate)));check(begin<end,"Spectral pitch search empty");
        auto best=begin;for(auto i=begin+1;i<=end;++i)if(std::norm(spectrum[i])>std::norm(spectrum[best]))best=i;return best;};
    const auto fundamental=peak(wanted*.9,wanted*1.1),global=peak(wanted*.25,std::min(wanted*4,double(rate)*.49));
    const auto relative=std::abs(spectrum[fundamental])/std::abs(spectrum[global]);
    check(std::isfinite(relative) && relative>.01,"Requested fundamental absent from rendered spectrum");
    return {double(fundamental)*rate/double(n),double(global)*rate/double(n),relative};
}
void renderedClock(Session s,const std::filesystem::path &root,double baked){
    auto &t=s.tracks[0];t.clips[1].startFrame=t.clips[0].startFrame+t.clips[0].lengthFrames;validate(s);const auto original=s;
    ResourceLedger ledger(192*1024*1024,"Regenerated clock DSP");
    {
        const auto &c=t.clips[0];auto envelope=prepareCompHandleEnvelope(s,t.id,c.id,c.startFrame-32,c.startFrame+c.lengthFrames+32,context,ledger);
        auto span=prepareCompSpan(envelope,ledger);const auto &r=envelope.data().regeneration->data();
        check(double(r.bakedPitchScale.frame)+double(r.bakedPitchScale.fraction)/double(r.bakedPitchScale.denominator)==baked,"Unexpected fixture baked speed");
        check(span.outputSampleRate()==48000 && span.source().sampleRate==r.raw.sampleRate,"Lost output/source clock distinction");
        MediaCacheConfig config;config.resources=ledger;config.maximumOpenFiles=1;config.pageFrames=512;config.cacheBudgetBytes=128*1024;config.registryBudgetBytes=256*1024;
        MediaReadCache cache(root,std::span<const Asset>(&span.source(),1),s.sampleRate,config);
        std::vector<float> output;const auto result=renderCompSpan(span,[&](Frame first,std::span<float> v){cache.read(0,first,v);},[&](auto v){output.insert(output.end(),v.begin(),v.end());},ledger,[]{});
        const auto scale=std::exp2(double(r.originalNominalClip.stretch->settings.pitchMilliCents)/1200000.)*baked;
        check(result.writtenFrames==span.outputFrames() && output.size()==std::size_t(span.outputFrames())*2,"Regenerated DSP duration changed");
        const auto expectedFactor=scale*double(span.source().sampleRate)/span.outputSampleRate()*double(span.grid().denominator())/span.grid().numerator();
        check(std::abs(result.backendPitchFactor/expectedFactor-1)<1e-12,"Clock-aware backend pitch factor differs");
        Json channels=Json::array();for(unsigned channel=0;channel<2;++channel){const auto wanted=(channel?1703.:997.)*scale;
            const auto observed=frequency(output,channel,span.outputSampleRate(),wanted);check(std::abs(observed.fundamental/wanted-1)<.01,"Regenerated spectral pitch differs by more than one percent");
            channels.push_back({{"wantedHz",wanted},{"measuredHz",observed.fundamental},{"globalPeakHz",observed.globalPeak},{"relativeFundamental",observed.fundamentalRelativeAmplitude}});
        }
        const auto name="clock-span-"+std::to_string(clockRenders.size())+".pcm";std::ofstream f(root/name,std::ios::binary);f.write(reinterpret_cast<const char *>(output.data()),std::streamsize(output.size()*sizeof(float)));check(bool(f),"Cannot retain clock DSP samples");
        clockRenders.push_back({{"rawRate",span.source().sampleRate},{"outputRate",span.outputSampleRate()},{"bakedSpeed",baked},{"currentPlaybackRate",{c.playbackRate.numerator,c.playbackRate.denominator}},{"pitchMilliCents",span.settings().pitchMilliCents},{"processingFrames",span.processingFrames()},{"outputFrames",span.outputFrames()},{"backendPitchFactor",result.backendPitchFactor},{"floatPeak",result.floatPeak},{"channels",channels},{"pcm",name},{"ownedRegeneration",false}});
    }
    check(s==original && !ledger.usage().reservedBytes,"Clock DSP changed canonical state or leaked memory");
}
void plannedRoundtrip(Session s){
    // Move the current fragments adjacent after metadata-only cropping. Their
    // immutable historical positions stay intact in the source closure.
    auto &t=s.tracks[0];t.clips[1].startFrame=t.clips[0].startFrame+t.clips[0].lengthFrames;
    validate(s);const auto original=s;const auto boundary=t.clips[1].startFrame;
    const PrepareCompTransition edit{t.id,t.clips[0].id,t.clips[1].id,boundary-32,boundary+32,ClipFadeCurve::EqualPower};
    const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",std::string(64,'a')};
    ResourceLedger ledger(192*1024*1024,"Regeneration recipe");
    {
        auto plan=prepareCompProcessing(s,edit,context,8,ledger);
        for(unsigned side=0;side<2;++side){const auto &h=side?plan.handles().incoming():plan.handles().outgoing();const auto &e=h.data();
            check(bool(e.regeneration) && !e.nominalAsset && e.raw.id!=e.base.id,"Regeneration envelope lost original source distinction");
            const auto &r=e.regeneration->data();
            check(e.needsNewSpan && e.map.front().output==SourcePosition{} && e.map.back().output==SourcePosition{e.outputFrames,0,1},"Repeated envelope failed to extend current artifact");
            // A fresh playback-source declaration validates independently against
            // the existing schema's layered raw-map reconstruction.
            const auto &p=side?plan.incoming():plan.outgoing();auto asset=e.base;asset.id=Id::generate();asset.sha256=std::string(64,'a');asset.nativeEvidence.clear();
            asset.frames=p.pcm.prefixFrames+p.pcm.currentFrames+p.pcm.suffixFrames;
            asset.sampleRate=p.domain==CompProcessingDomain::ProjectAfterSrc?s.sampleRate:e.base.sampleRate;
            const auto selection=Id::generate();asset.relativePath="media/comp-results/"+selection.str()+"/"+asset.id.str()+".wav";
            auto playback=prepareCompPlaybackSource(s,context,plan,side,asset,ledger);auto candidate=s;
            CompAssemblyReference assembly{Id::generate(),selection,std::string(64,'b'),{}};assembly.outputs[side]=asset.id;
            candidate.assets.push_back(asset);candidate.compPairSelections.push_back({selection,std::string(64,'c')});
            candidate.compPlaybackSources.push_back({assembly,side,std::string(64,'c'),playback.data()});candidate.tracks[0].clips[side]=playback.data().playback;validate(candidate);
            const auto oldMap=clipSourceMap(e.expected,e.base.sampleRate,s.sampleRate);
            const auto newMap=clipSourceMap(playback.data().playback,asset.sampleRate,s.sampleRate);
            for(Frame f=0;f<e.expected.lengthFrames;f+=17)
                check(playback.rawPosition(newMap.at(e.expected.startFrame-playback.data().playback.startFrame+f))==compRegenerationRawPosition(r,oldMap.at(f)),"Repeated plan shifted original audible raw timing");
            auto span=prepareCompSpan(h,ledger);
            check(span.source()==r.raw && span.outputSampleRate()==e.base.sampleRate && span.backendClockScale()==r.backendClockScale &&
                span.settings()==r.originalNominalClip.stretch->settings,"Repeated span lost original settings or clock");
        }
        const auto operation=Id::generate();auto recipe=encodeCompProcessingRecipe(s,context,plan,operation,executor,ledger);
        try{(void)encodeCompSpanRequest(recipe,executor,0,Id::generate(),std::string(64,'b'),ledger);throw std::runtime_error("Unverified repeated owned request enabled");}
        catch(const ProjectError &error){check(error.code()==ErrorCode::UnsupportedSchema,"Repeated owned request lost integration gate");}
        const auto j=Json::parse(recipe.bytes());check(j.at("format").get<std::string>()==compRegenerationRecipeFormat,"Repeated recipe used legacy schema");
        auto reopenedContext=context;++reopenedContext.projectEpoch;reopenedContext.rootIdentity="relocated-history";
        auto reopened=decodeCompProcessingRecipe(recipe.bytes(),operation,reopenedContext,executor,recipe.digest(),ledger);
        check(recipe.bytes()==reopened.bytes() && recipe.digest()==reopened.digest(),"Repeated recipe changed canonical identity");
        verifyCompProcessingCurrent(s,reopenedContext,reopened.plan());
        for(unsigned change=0;change<5;++change){auto changed=s;
            if(change==0)changed.compPlaybackSources.back().selectionSha256=std::string(64,'0');
            if(change==1)changed.compPairSelections.front().selectionSha256=std::string(64,'0');
            if(change==2)changed.tracks[0].takeLanes[0].clips[0].processing.gainDb+=1;
            if(change==3)changed.assets.front().sha256=std::string(64,'0');
            if(change==4)changed.compPlaybackSources.erase(changed.compPlaybackSources.begin());
            refuses([&]{verifyCompProcessingCurrent(changed,reopenedContext,reopened.plan());});
        }
        auto shuffled=s;std::reverse(shuffled.assets.begin(),shuffled.assets.end());std::reverse(shuffled.compPlaybackSources.begin(),shuffled.compPlaybackSources.end());std::reverse(shuffled.compPairSelections.begin(),shuffled.compPairSelections.end());
        auto stable=encodeCompProcessingRecipe(shuffled,context,plan,operation,executor,ledger);check(stable.bytes()==recipe.bytes(),"History container order changed recipe identity");
        auto tiny=CompRecipeLimits{};tiny.maximumBytes=256;refuses([&]{encodeCompProcessingRecipe(s,context,plan,operation,executor,ledger,tiny);});
        check(s==original,"Repeated planning changed canonical state");++plannedRecipes;
    }
    check(!ledger.usage().reservedBytes,"Repeated recipe leaked joined resources");
}
void mixedAdmission(Session s){
    auto &t=s.tracks[0];auto &out=t.clips[0];auto &in=t.clips[1];
    out.sourceFrame+=64;out.lengthFrames-=128;
    const auto *history=findCompPlaybackSource(s,in.assetId);check(history,"Missing mixed source history");
    in=history->source.original;in.sourceFrame=0;in.sourceTiming={};in.lengthFrames=1000;in.startFrame=out.startFrame+out.lengthFrames;
    validate(s);const auto original=s;const auto boundary=in.startFrame;
    const PrepareCompTransition edit{t.id,out.id,in.id,boundary-32,boundary+32,ClipFadeCurve::EqualPower};
    const CompRecipeExecutor executor{"soundcurrent.comp-join-quintic-v1",std::string(64,'a')};ResourceLedger ledger(192*1024*1024,"Mixed regeneration admission");
    {
        auto plan=prepareCompProcessing(s,edit,context,8,ledger);
        check(plan.outgoing().domain==CompProcessingDomain::ExistingMedia && plan.incoming().domain!=CompProcessingDomain::ExistingMedia &&
            plan.handles().outgoing().data().regeneration && !plan.handles().incoming().data().regeneration,"Mixed corpus failed to reach unchanged assembled side");
        auto recipe=encodeCompProcessingRecipe(s,context,plan,Id::generate(),executor,ledger);
        const auto unsupported=[&](auto f){try{f();throw std::runtime_error("Mixed unverified history admitted");}
            catch(const ProjectError &error){check(error.code()==ErrorCode::UnsupportedSchema,"Mixed history lacked explicit producer refusal");}};
        unsupported([&]{(void)encodeCompSpanRequest(recipe,executor,1,Id::generate(),std::string(64,'b'),ledger);});
        unsupported([&]{(void)prepareCompPairSelection(std::move(recipe),executor,std::string(64,'b'),{Id::generate(),Id::generate()},{},ledger);});
    }
    check(s==original && !ledger.usage().reservedBytes,"Mixed admission modified state or leaked resources");
}
}
int main(){try{
    Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());
    auto s=decodeProject(bytes(root/"adopted-project.json"));validate(s);const auto original=s;
    for(const auto &path:input.at("legacyRequests")){
        const auto request=Json::parse(bytes(utf8Path(path.get<std::string>())));
        const auto &e=request.at("recipeExecutor");const CompRecipeExecutor executor{e.at("id"),e.at("buildSha256")};
        auto old=decodeCompProcessingRecipe(request.at("recipe").get<std::string>(),Id(request.at("recipeOperation").get<std::string>()),context,executor,request.at("recipeDigest").get<std::string>(),ResourceLedger(192*1024*1024,"Legacy recipe"));
        check(old.bytes()==request.at("recipe").get<std::string>() && Json::parse(old.bytes()).at("format").get<std::string>()==compProcessingRecipeFormat,"Retained legacy recipe identity changed");
        const auto requestPath=utf8Path(path.get<std::string>());const auto saved=bytes(requestPath);
        auto legacySpan=decodeCompSpanRequest(saved,hashMediaFile(requestPath),context,request.at("executor").at("buildSha256").get<std::string>(),ResourceLedger(192*1024*1024,"Legacy span request"));
        check(legacySpan.bytes()==saved && legacySpan.span().outputSampleRate()==legacySpan.span().source().sampleRate && legacySpan.span().backendClockScale()==SourcePosition{1,0,1},"Retained legacy span request or clock changed");++legacyRecipes;
    }
    renderedClock(s,root,1.5);
    auto currentSpeed=s;currentSpeed.tracks[0].clips[0].lengthFrames/=2;currentSpeed.tracks[0].clips[0].playbackRate={5,4};renderedClock(currentSpeed,root,1.5);
    plannedRoundtrip(s);
    mixedAdmission(s);
    check(s.compPlaybackSources.size()==2,"Expected actual paired adoption fixture");
    ResourceLedger ledger(32*1024*1024,"Regeneration source test");Json reports=Json::array();
    for(unsigned side=0;side<2;++side){const auto &c=s.tracks[0].clips[side];
        {auto resolved=resolveCompRegenerationSource(s,s.tracks[0].id,c.id,context,ledger);const auto &r=resolved.data();
            check(r.expected==c && r.current.id==c.assetId && r.raw.id!=c.assetId && r.originalNominal && r.originalNominalClip.stretch,"Assembled output reinterpreted as raw/nominal");
            const auto *source=findCompPlaybackSource(s,c.assetId);check(source,"Actual source provenance missing");
            check(r.originalNominalClip==source->source.original && *r.originalNominal==source->source.base,"Original nominal source not retained");
            const auto expectedPitch=source->source.domain==CompProcessingDomain::ProjectAfterSrc?
                scaleSourcePosition({1,0,1},source->source.original.playbackRate.numerator,source->source.original.playbackRate.denominator):SourcePosition{1,0,1};
            check(r.bakedPitchScale==expectedPitch && r.backendClockScale==scaleSourcePosition(expectedPitch,r.raw.sampleRate,r.current.sampleRate),"Baked varispeed/sample clock lost");
            check(r.history.size()==2 && r.selections.size()==1 && r.retained.size()==1,"Paired ancestry closure incomplete");
            std::size_t compared=0;
            for(Frame f=0;f<r.current.frames;++f){const SourcePosition at{f,1,7};check(compRegenerationRawPosition(r,at)==compPlaybackRawPosition(source->source,at),"Normalized raw map changed fractional timing");++compared;}
            check(compRegenerationRawPosition(r,{r.current.frames,0,1})==compPlaybackRawPosition(source->source,{r.current.frames,0,1}),"Exclusive raw endpoint differs");
            refuses([&]{compRegenerationRawPosition(r,{r.current.frames,1,7});});
            verifyCompRegenerationSourceCurrent(s,context,resolved);
            for(unsigned mutation=0;mutation<9;++mutation){auto changed=s;auto owner=context;
                if(mutation==0)changed.id=Id::generate();
                if(mutation==1)changed.tracks[0].clips[side].processing.gainDb+=1;
                if(mutation==2)changed.assets[0].sha256=std::string(64,'0');
                if(mutation==3)changed.compPairSelections[0].selectionSha256=std::string(64,'0');
                if(mutation==4)changed.compPlaybackSources[1-side].selectionSha256=std::string(64,'0');
                if(mutation==5)changed.tracks[0].takeLanes[0].clips[0].processing.gainDb+=1;
                if(mutation==6)++owner.projectEpoch;
                if(mutation==7)owner.rootIdentity="different-root";
                if(mutation==8)changed.assets.erase(std::find_if(changed.assets.begin(),changed.assets.end(),[&](const auto &a){return a.id==r.originalNominal->id;}));
                refuses([&]{verifyCompRegenerationSourceCurrent(changed,owner,resolved);});
            }
            auto unrelated=s;unrelated.name="Unrelated title edit";verifyCompRegenerationSourceCurrent(unrelated,context,resolved);
            reports.push_back({{"side",side},{"currentFrames",r.current.frames},{"fractionalPositionsCompared",compared},{"closureRecords",r.history.size()},{"sourceRate",r.raw.sampleRate},{"artifactRate",r.current.sampleRate}});
            const auto reserved=ledger.usage().reservedBytes;auto moved=std::move(resolved);refuses([&]{resolved.data();});check(ledger.usage().reservedBytes==reserved,"Move released source credit");verifyCompRegenerationSourceCurrent(s,context,moved);
        }check(ledger.usage().reservedBytes==0,"Source resolution leaked joined memory");
    }
    std::size_t depth=0;auto nested=s;SourcePosition pitch;
    for(unsigned pass=0;pass<3;++pass){
        auto before=resolveCompRegenerationSource(nested,nested.tracks[0].id,nested.tracks[0].clips[0].id,context,ledger);
        pitch=before.data().bakedPitchScale;const bool post=pass!=1;const bool crop=pass!=1;
        nested=layer(nested,post,crop,ledger);auto resolved=resolveCompRegenerationSource(nested,nested.tracks[0].id,nested.tracks[0].clips[0].id,context,ledger);const auto &r=resolved.data();
        const auto expected=post?scaleSourcePosition(pitch,3,2):pitch;
        check(r.bakedPitchScale==expected,"Nested pitch scale accumulated incorrectly");
        check(r.raw==s.compPlaybackSources[0].source.raw && r.originalNominal==std::optional<Asset>(s.compPlaybackSources[0].source.base),"Nested resolution lost original source");
        check(r.history.size()==3+pass,"Nested source closure omitted ancestor/sibling");
        const auto &leaf=*findCompPlaybackSource(nested,nested.tracks[0].clips[0].assetId);
        for(Frame f=0;f<r.current.frames;f+=17)check(compRegenerationRawPosition(r,{f,2,5})==compPlaybackRawPosition(leaf.source,{f,2,5}),"Nested crop map differs");
        check(decodeProject(encodeProject(nested))==nested,"Regeneration lineage changed across codec");plannedRoundtrip(nested);renderedClock(nested,root,pass==0?2.25:(pass==1?2.25:3.375));++depth;
    }
    check(ledger.usage().reservedBytes==0,"Nested resolution retained resources");
    const auto &c=s.tracks[0].clips[0];ResourceLedger tiny(1,"Tiny regeneration");
    refuses([&]{resolveCompRegenerationSource(s,s.tracks[0].id,c.id,context,tiny);});check(tiny.usage().reservedBytes==0,"Memory refusal leaked");
    for(const auto limit:{std::size_t(0),std::size_t(1),std::size_t(16389)})refuses([&]{resolveCompRegenerationSource(s,s.tracks[0].id,c.id,context,ledger,limit);});
    auto invalid=s;invalid.compPlaybackSources.clear();refuses([&]{resolveCompRegenerationSource(invalid,invalid.tracks[0].id,c.id,context,ledger);});
    invalid=s;invalid.compPlaybackSources[0].source.rawMap[0].raw.frame++;refuses([&]{resolveCompRegenerationSource(invalid,invalid.tracks[0].id,c.id,context,ledger);});
    check(ledger.usage().reservedBytes==0 && s==original,"Refusal changed state or leaked resources");
    Json result{{"clockRenders",clockRenders},{"checks",checks},{"mixedAdmissionRefused",true},{"plannedRecipes",plannedRecipes},{"legacyRecipes",legacyRecipes},{"actualAdoptedSourceCases",reports},{"metadataOnlyAdditionalGenerations",depth},{"stateUnchanged",true},{"joinedMemoryReleased",true},{"fileVerification",false},{"newRendering",true},{"ownedRegeneration",false},{"qualityAdmission",false}};
    std::ofstream out(root/"regeneration-source-report.json");out<<result.dump(2)<<'\n';check(bool(out),"Report write failed");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
