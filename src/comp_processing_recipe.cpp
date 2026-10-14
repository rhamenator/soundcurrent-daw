// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_processing_recipe.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/comp_regeneration_source.hpp>
#include "media_io.hpp"
#include "warp_codec.hpp"
#include <algorithm>
#include <array>
#include <set>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
void check(bool v,const char *m,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,m);}
void poll(std::stop_token t){check(!t.stop_requested(),"Comp recipe canceled",ErrorCode::Canceled);}
void policy(const CompRecipeLimits &l){check(l.maximumBytes>0 && l.maximumBytes<=8*1024*1024 && l.codecBytes>=32*1024*1024 && l.codecBytes<=256*1024*1024,"Invalid comp recipe bounds");}
void executor(const CompRecipeExecutor &e){check(e.id=="soundcurrent.comp-join-quintic-v1" && e.buildSha256.size()==64 && std::all_of(e.buildSha256.begin(),e.buildSha256.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Unsupported comp recipe executor");}
ProjectBudget budget(const CompRecipeLimits &l){return {{8*1024*1024},l.maximumBytes,l.codecBytes/8,0};}
template<class T> const T &find(const std::vector<T> &v,const Id &id){auto i=std::find_if(v.begin(),v.end(),[&](const auto &x){return x.id==id;});check(i!=v.end(),"Comp recipe source missing");return *i;}
Session minimal(const Session &s,const CompProcessingPlan &plan){
    const auto &e=plan.handles().transition();
    const auto &t=find(s.tracks,e.track);const auto &a=find(t.clips,e.outgoing),&b=find(t.clips,e.incoming);
    check(a.takeOrigin && b.takeOrigin,"Comp recipe requires retained take sources");
    Session m;m.id=s.id;m.name="Comp processing recipe";m.sampleRate=s.sampleRate;
    std::set<std::string> assets;
    const auto sourceAssets=[&](const Clip &c){assets.insert(c.assetId.str());if(c.stretch)assets.insert(c.stretch->sourceAssetId.str());if(c.compHandles)assets.insert(c.compHandles->baseAssetId.str());};
    std::vector<ClipTakeOrigin> origins{*a.takeOrigin,*b.takeOrigin};
    std::set<std::string> histories,selections;
    for(const auto *h:{&plan.handles().outgoing(),&plan.handles().incoming()})if(h->data().regeneration){
        const auto &r=h->data().regeneration->data();
        for(const auto &asset:r.assets)assets.insert(asset.id.str());
        for(const auto &retained:r.retained)if(std::find(origins.begin(),origins.end(),retained.origin)==origins.end())origins.push_back(retained.origin);
        for(const auto &history:r.history)if(histories.insert(history.source.rendered.id.str()).second)m.compPlaybackSources.push_back(history);
        for(const auto &selection:r.selections)if(selections.insert(selection.operation.str()).second)m.compPairSelections.push_back(selection);
    }
    std::sort(m.compPlaybackSources.begin(),m.compPlaybackSources.end(),[](const auto &x,const auto &y){return x.source.rendered.id.str()<y.source.rendered.id.str();});
    std::sort(m.compPairSelections.begin(),m.compPairSelections.end(),[](const auto &x,const auto &y){return x.operation.str()<y.operation.str();});
    for(const auto &original:s.tracks){
        bool needed=original.id==e.track;for(const auto &origin:origins)needed=needed || origin.trackId==original.id;if(!needed)continue;
        Track selected;selected.id=original.id;selected.name="Source";selected.layout=original.layout;selected.eq.id=original.eq.id;
        if(original.id==e.track){selected.clips={a,b};sourceAssets(a);sourceAssets(b);}
        for(const auto &lane:original.takeLanes){TakeLane chosen=lane;chosen.name="Retained source";chosen.clips.clear();
            for(const auto &clip:lane.clips)if(std::any_of(origins.begin(),origins.end(),[&](const auto &o){return o.trackId==original.id && o.laneId==lane.id && o.clipId==clip.id;})){chosen.clips.push_back(clip);sourceAssets(clip);}
            std::sort(chosen.clips.begin(),chosen.clips.end(),[](const auto &x,const auto &y){return x.id.str()<y.id.str();});
            if(!chosen.clips.empty())selected.takeLanes.push_back(std::move(chosen));
        }
        std::sort(selected.takeLanes.begin(),selected.takeLanes.end(),[](const auto &x,const auto &y){return x.id.str()<y.id.str();});m.tracks.push_back(std::move(selected));
    }
    std::sort(m.tracks.begin(),m.tracks.end(),[](const auto &x,const auto &y){return x.id.str()<y.id.str();});
    for(const auto &id:assets)m.assets.push_back(find(s.assets,Id(id)));
    validate(m,{8*1024*1024});return m;
}
// SAX admission precedes DOM construction, including duplicate-key tracking.
struct RecipePreflight : nlohmann::json_sax<Json> {
    PayloadCharge charge;
    std::vector<std::set<std::string>> levels;
    explicit RecipePreflight(std::size_t bytes):charge("Comp recipe parser staging",bytes){}
    bool scalar(){charge.add(128);return true;}
    bool null() override {return scalar();}
    bool boolean(bool) override {return scalar();}
    bool number_integer(number_integer_t) override {return scalar();}
    bool number_unsigned(number_unsigned_t) override {return scalar();}
    bool number_float(number_float_t,const string_t &) override {return scalar();}
    bool string(string_t &v) override {charge.add(v.size());return scalar();}
    bool binary(binary_t &) override {check(false,"Binary comp recipe refused");return false;}
    bool start(){check(levels.size()<16,"Comp recipe nesting exceeds bound");charge.add(512);levels.emplace_back();return true;}
    bool start_object(std::size_t) override {return start();}
    bool start_array(std::size_t) override {return start();}
    bool key(string_t &v) override {charge.add(128);charge.add(v.size());check(!levels.empty() && levels.back().insert(v).second,"Duplicate comp recipe field");return true;}
    bool end_object() override {levels.pop_back();return true;}
    bool end_array() override {levels.pop_back();return true;}
    bool parse_error(std::size_t,const std::string &,const nlohmann::detail::exception &) override {check(false,"Malformed comp recipe JSON");return false;}
};
Json parse(std::string_view bytes,std::size_t budgetBytes){
    RecipePreflight preflight(budgetBytes);preflight.charge.add(bytes.size());
    check(Json::sax_parse(bytes.begin(),bytes.end(),&preflight),"Malformed comp recipe JSON");
    return Json::parse(bytes.begin(),bytes.end());
}
Json blend(const std::optional<CompHandleInnerBlend> &b){if(!b)return nullptr;return {{"frames",b->frames},{"permittedFirst",b->permittedFirst},{"permittedEnd",b->permittedEnd}};}
Json fragment(const CompHandleEnvelope &envelope,const CompFragmentProcessing &p){
    const auto &e=envelope.data();Json map=Json::array();for(const auto &point:e.map)map.push_back({{"raw",warp_codec::position(point.raw)},{"output",warp_codec::position(point.output)}});
    Json result={{"clip",e.expected.id.str()},{"domain",int(p.domain)},{"pcm",{{"channels",p.pcm.channels},{"blockFrames",p.pcm.blockFrames},{"currentFrames",p.pcm.currentFrames},{"prefixFrames",p.pcm.prefixFrames},{"suffixFrames",p.pcm.suffixFrames}}},
        {"blends",{{"prefix",blend(p.blends.prefix)},{"suffix",blend(p.blends.suffix)}}},{"map",map},{"projectFirst",e.projectFirst},{"projectEnd",e.projectEnd},
        {"outputOrigin",warp_codec::position(e.clipOutputOrigin)},{"assets",{{"raw",e.raw.id.str()},{"current",e.base.id.str()},{"retained",e.retainedAsset.id.str()},{"nominal",e.nominalAsset?Json(e.nominalAsset->id.str()):Json(nullptr)}}}};
    if(e.regeneration){const auto &r=e.regeneration->data();result["regeneration"]={{"originalClip",r.originalNominalClip.id.str()},
        {"originalNominal",r.originalNominal?Json(r.originalNominal->id.str()):Json(nullptr)},
        {"bakedPitchScale",warp_codec::position(r.bakedPitchScale)},{"backendClockScale",warp_codec::position(r.backendClockScale)}};}
    return result;
}
Json document(const Session &s,const CompProcessingPlan &p,const Id &op,const CompRecipeExecutor &e,const CompRecipeLimits &l){
    const auto format=p.handles().outgoing().data().regeneration || p.handles().incoming().data().regeneration?compRegenerationRecipeFormat:compProcessingRecipeFormat;
    const auto &t=p.handles().transition();return {{"format",format},{"operation",op.str()},{"executor",{{"id",e.id},{"buildSha256",e.buildSha256}}},
        {"curve","complementary-quintic-v1"},{"resampler",positionedResamplingAlgorithmId},{"selection",encodeProject(minimal(s,p),budget(l))},
        {"maximumBlendProjectFrames",p.maximumBlendProjectFrames()},{"transition",{{"track",t.track.str()},{"outgoing",t.outgoing.str()},{"incoming",t.incoming.str()},{"start",t.startFrame},{"end",t.endFrame},{"curve",int(t.curve)}}},
        {"outgoing",fragment(p.handles().outgoing(),p.outgoing())},{"incoming",fragment(p.handles().incoming(),p.incoming())}};
}
std::string digest(std::string_view bytes){media_io::SampleHash h;h.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});return h.digest();}
std::string relative(const Id &op){return "media/comp-recipes/"+op.str()+".json";}
void mkdir(const std::filesystem::path &p){if(!std::filesystem::exists(std::filesystem::symlink_status(p))){check(std::filesystem::create_directory(p),"Cannot create comp recipe folder",ErrorCode::Io);media_io::flushDirectory(p.parent_path());}media_io::plainDirectory(p);}
}
CompProcessingRecipe decodeCompProcessingRecipe(std::string_view bytes,const Id &op,const CompHandlePlanContext &context,
    const CompRecipeExecutor &expected,std::string_view expectedDigest,ResourceLedger ledger,CompRecipeLimits l,std::stop_token stop){
    policy(l);executor(expected);poll(stop);check(!bytes.empty() && bytes.size()<=l.maximumBytes,"Comp recipe exceeds byte limit",ErrorCode::ResourceLimit);
    check(expectedDigest.size()==64 && std::all_of(expectedDigest.begin(),expectedDigest.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid trusted comp recipe digest");
    auto work=ledger.reserve(l.codecBytes);auto retained=ledger.reserve(bytes.size()*2+65536);
    try{
        const auto j=parse(bytes,l.codecBytes/8);check(j.is_object() && j.size()==10 && j.at("format").is_string() && (j.at("format")==compProcessingRecipeFormat || j.at("format")==compRegenerationRecipeFormat) && j.at("operation")==op.str(),"Unknown comp recipe identity/schema");
        check(warp_codec::exact(j.at("executor"),Json{{"id",expected.id},{"buildSha256",expected.buildSha256}}),"Comp executor differs from trusted identity");
        check(j.at("selection").is_string(),"Missing comp source snapshot");auto s=decodeProject(j.at("selection").get_ref<const std::string &>(),budget(l));
        const auto &t=j.at("transition");check(t.is_object() && t.size()==6 && t.at("track").is_string() && t.at("outgoing").is_string() && t.at("incoming").is_string(),"Invalid comp transition fields");
        const auto integer=[](const Json &v){return Frame(warp_codec::unsignedInteger(v,std::uint64_t(INT64_MAX)));};
        PrepareCompTransition transition{Id(t.at("track").get<std::string>()),Id(t.at("outgoing").get<std::string>()),Id(t.at("incoming").get<std::string>()),integer(t.at("start")),integer(t.at("end")),ClipFadeCurve(warp_codec::unsignedInteger(t.at("curve"),2))};
        const auto maximum=Frame(warp_codec::unsignedInteger(j.at("maximumBlendProjectFrames"),1000000000));auto p=prepareCompProcessing(s,transition,context,maximum,ledger,l.planning);poll(stop);
        check(warp_codec::exact(j,document(s,p,op,expected,l)),"Saved comp scope/source snapshot differs from recomputed plan");
        auto canonical=j.dump();check(canonical.size()<=l.maximumBytes,"Canonical comp recipe exceeds limit",ErrorCode::ResourceLimit);const auto hash=digest(canonical);check(hash==expectedDigest,"Comp recipe differs from trusted selection digest",ErrorCode::MediaMismatch);
        return {std::move(retained),std::move(p),op,std::move(canonical),hash};
    }catch(const Json::exception &){throw ProjectError(ErrorCode::InvalidState,"Invalid comp recipe JSON");}
}
CompProcessingRecipe encodeCompProcessingRecipe(const Session &s,const CompHandlePlanContext &context,const CompProcessingPlan &p,
    const Id &op,const CompRecipeExecutor &e,ResourceLedger ledger,CompRecipeLimits l,std::stop_token stop){
    policy(l);executor(e);poll(stop);verifyCompProcessingCurrent(s,context,p);auto work=ledger.reserve(l.codecBytes);auto encoded=ledger.reserve(l.maximumBytes*2);
    const auto bytes=document(s,p,op,e,l).dump();check(bytes.size()<=l.maximumBytes,"Comp recipe exceeds byte limit",ErrorCode::ResourceLimit);
    work.resize(0);return decodeCompProcessingRecipe(bytes,op,context,e,digest(bytes),ledger,l,stop);
}
std::string persistCompProcessingRecipe(const std::filesystem::path &root,const Session &s,const CompHandlePlanContext &context,
    const CompProcessingPlan &p,const Id &op,const CompRecipeExecutor &e,ResourceLedger ledger,CompRecipeLimits l,std::stop_token stop){
    check(root.is_absolute() && root.native().size()<=4096 && root.native().find(std::filesystem::path::value_type(0))==root.native().npos,
        "Comp recipe root must be an explicit absolute directory",ErrorCode::InvalidParameter);
    auto recipe=encodeCompProcessingRecipe(s,context,p,op,e,ledger,l,stop);
    media_io::plainDirectory(root);mkdir(root/"media");mkdir(root/"media"/"comp-recipes");const auto folder=root/"media"/"comp-recipes";
    const bool fresh=!std::filesystem::exists(std::filesystem::symlink_status(folder/"writer.lock"));media_io::JobLease lock(folder,fresh,!fresh);
    check(lock.status()==media_io::LeaseStatus::Held,"Comp recipe publication busy",ErrorCode::Io);
    const auto destination=root/utf8Path(relative(op)),temporary=folder/("recipe-"+Id::generate().str()+".partial");bool owned=false;
    try{poll(stop);media_io::File file(temporary,true);owned=true;file.write(recipe.bytes());file.flush();file.close();poll(stop);media_io::publishMedia(temporary,destination);owned=false;}
    catch(...){if(owned){std::error_code ignored;std::filesystem::remove(temporary,ignored);}throw;}
    return recipe.digest();
}
CompProcessingRecipe loadCompProcessingRecipe(const std::filesystem::path &root,const Session &current,const CompHandlePlanContext &context,
    const Id &op,const CompRecipeExecutor &executor,std::string_view expectedDigest,ResourceLedger ledger,CompRecipeLimits l,std::stop_token stop){
    policy(l);poll(stop);ApprovedMediaRoot approved(root,ledger,{1});auto file=approved.open(relative(op),l.maximumBytes,stop);
    auto credit=ledger.reserve(std::size_t(file.size())*2);std::string bytes(std::size_t(file.size()),'\0');
    for(std::size_t at=0;at<bytes.size();at+=65536){poll(stop);file.readAt(at,{bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at)},stop);}
    auto recipe=decodeCompProcessingRecipe(bytes,op,context,executor,expectedDigest,ledger,l,stop);file.verifyUnchanged();poll(stop);verifyCompProcessingCurrent(current,context,recipe.plan());return recipe;
}
} // namespace soundcurrent::daw
