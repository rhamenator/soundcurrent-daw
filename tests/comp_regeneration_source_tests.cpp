// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_regeneration_source.hpp>
#include <soundcurrent/comp_playback_state.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;unsigned checks=0;
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
}
int main(){try{
    Json input;std::cin>>input;const auto root=utf8Path(input.at("root").get<std::string>());
    auto s=decodeProject(bytes(root/"adopted-project.json"));validate(s);const auto original=s;
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
        check(decodeProject(encodeProject(nested))==nested,"Regeneration lineage changed across codec");++depth;
    }
    check(ledger.usage().reservedBytes==0,"Nested resolution retained resources");
    const auto &c=s.tracks[0].clips[0];ResourceLedger tiny(1,"Tiny regeneration");
    refuses([&]{resolveCompRegenerationSource(s,s.tracks[0].id,c.id,context,tiny);});check(tiny.usage().reservedBytes==0,"Memory refusal leaked");
    for(const auto limit:{std::size_t(0),std::size_t(1),std::size_t(16389)})refuses([&]{resolveCompRegenerationSource(s,s.tracks[0].id,c.id,context,ledger,limit);});
    auto invalid=s;invalid.compPlaybackSources.clear();refuses([&]{resolveCompRegenerationSource(invalid,invalid.tracks[0].id,c.id,context,ledger);});
    invalid=s;invalid.compPlaybackSources[0].source.rawMap[0].raw.frame++;refuses([&]{resolveCompRegenerationSource(invalid,invalid.tracks[0].id,c.id,context,ledger);});
    check(ledger.usage().reservedBytes==0 && s==original,"Refusal changed state or leaked resources");
    Json result{{"checks",checks},{"actualAdoptedSourceCases",reports},{"metadataOnlyAdditionalGenerations",depth},{"stateUnchanged",true},{"joinedMemoryReleased",true},{"fileVerification",false},{"newRendering",false},{"qualityAdmission",false}};
    std::ofstream out(root/"regeneration-source-report.json");out<<result.dump(2)<<'\n';check(bool(out),"Report write failed");std::cout<<result.dump()<<'\n';return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
