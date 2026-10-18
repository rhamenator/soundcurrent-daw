// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/stretch_recovery.hpp>
#include <soundcurrent/approved_media.hpp>
#include "media_io.hpp"
#include "warp_codec.hpp"
#include <algorithm>
#include <array>
#include <set>
namespace soundcurrent::daw {
namespace {
using Json=nlohmann::json;
constexpr std::size_t codecCredit=16*1024*1024;
constexpr std::string_view format="sc-stretch-selection-v1";
void check(bool v,const char *s,ErrorCode code=ErrorCode::InvalidState){if(!v)throw ProjectError(code,s);}
void poll(std::stop_token t){check(!t.stop_requested(),"Render recovery canceled",ErrorCode::Canceled);}
void limits(const StretchRecoveryLimits &l){
    check(l.maximumEntries>0 && l.maximumEntries<=1024 && l.maximumManifestBytes>0 && l.maximumManifestBytes<=512*1024 &&
          l.maximumTotalBytes>=l.maximumManifestBytes && l.maximumTotalBytes<=32*1024*1024,"Invalid trusted render inventory bounds");
}
ProjectBudget budget(){return {{8*1024*1024},512*1024,8*1024*1024,0};}
std::string relative(const Id &op){return "media/stretch-selections/"+op.str()+".json";}
Session minimal(const Session &s,const ClipStretchPlan &p){
    Session m;m.id=s.id;m.name="Stretch selection";m.sampleRate=s.sampleRate;
    const auto t=std::find_if(s.tracks.begin(),s.tracks.end(),[&](const auto &v){return v.id==p.trackId;});
    check(t!=s.tracks.end(),"Stretch target track missing");
    const auto clip=std::find_if(t->clips.begin(),t->clips.end(),[&](const auto &v){return v.id==p.expectedClip.id;});
    check(clip!=t->clips.end() && *clip==p.expectedClip,"Stretch selection target differs from project");
    Track target;target.id=t->id;target.name="Target";target.layout=t->layout;target.eq.id=t->eq.id;
    target.clips={p.expectedClip};m.tracks={std::move(target)};
    for(const auto &a:s.assets)if(a.id==p.source.id || a.id==p.expectedClip.assetId)m.assets.push_back(a);
    validate(m,budget().state);return m;
}
Json parse(std::string_view raw){
    std::array<std::set<std::string>,18> fields;
    auto callback=[&](int depth,Json::parse_event_t event,Json &v){
        check(depth>=0 && depth<16,"Render selection nesting exceeds bound");
        if(event==Json::parse_event_t::object_start)fields[std::size_t(depth+1)].clear();
        if(event==Json::parse_event_t::key)check(fields[std::size_t(depth)].insert(v.get<std::string>()).second,"Duplicate render selection field");
        return true;
    };
    auto j=Json::parse(raw.begin(),raw.end(),callback);
    check(j.is_object(),"Render selection must be an object");return j;
}
StretchRenderPolicy policy(const Json &j,const StretchRecoveryLimits &l){
    check(j.is_object() && j.size()==6,"Unexpected render selection policy");
    auto n=[&](const char *k,std::uint64_t max){return warp_codec::unsignedInteger(j.at(k),max);};
    return {n("memoryBytes",l.render.memoryBytes),n("maximumInputFrames",l.render.maximumInputFrames),
        n("maximumOutputBytes",l.render.maximumOutputBytes),n("maximumSourceBytes",l.render.maximumSourceBytes),
        n("deadlineMilliseconds",l.render.deadlineMilliseconds)};
}
Json policyJson(const StretchRenderPolicy &p){return {{"memoryBytes",p.memoryBytes},{"maximumInputFrames",p.maximumInputFrames},
    {"maximumOutputBytes",p.maximumOutputBytes},{"maximumSourceBytes",p.maximumSourceBytes},{"deadlineMilliseconds",p.deadlineMilliseconds},
    {"version",1}};}
void mkdir(const std::filesystem::path &p){
    if(!std::filesystem::exists(std::filesystem::symlink_status(p))){
        check(std::filesystem::create_directory(p),"Cannot create render selection folder",ErrorCode::Io);media_io::flushDirectory(p.parent_path());
    }
    media_io::plainDirectory(p);
}
StretchRecoveryEntry inspect(const std::filesystem::path &root,const Session &current,const Id &op,
                            ResourceLedger ledger,const StretchRecoveryLimits &l,std::stop_token stop){
    poll(stop);limits(l);auto work=ledger.reserve(codecCredit);
    ApprovedMediaRoot owned(root,ledger,{3});auto file=owned.open(relative(op),l.maximumManifestBytes,stop);
    auto bank=ledger.reserve(std::size_t(file.size())*2);std::string bytes(std::size_t(file.size()),'\0');
    for(std::size_t at=0;at<bytes.size();at+=65536){poll(stop);file.readAt(at,{bytes.data()+at,std::min<std::size_t>(65536,bytes.size()-at)},stop);}
    auto j=parse(bytes);check(j.size()==6 && j.at("format").is_string() && j.at("format").get_ref<const std::string &>()==format && j.at("operation").is_string() &&
        j.at("operation").get_ref<const std::string &>()==op.str() && j.at("selection").is_string() && j.at("renderKey").is_string(),"Unexpected render selection identity/fields");
    const auto saved=policy(j.at("policy"),l);check(warp_codec::unsignedInteger(j.at("policy").at("version"),1)==1,"Unsupported render selection policy");
    auto s=decodeProject(j.at("selection").get_ref<const std::string &>(),budget());
    check(s.tracks.size()==1 && s.tracks[0].clips.size()==1 && s.assets.size()>=1 && s.assets.size()<=2,"Expected minimal render selection");
    const auto &r=j.at("request");check(r.is_object(),"Missing render request");
    auto number=[&](const char *k,std::uint64_t max){return warp_codec::unsignedInteger(r.at(k),max);};
    check(r.at("pitchMilliCents").is_number_integer() && r.at("pitchMilliCents")>=-2400000 && r.at("pitchMilliCents")<=2400000 &&
        r.at("formantPreserved").is_boolean() && r.at("contextEnabled").is_boolean(),"Invalid render selection settings");
    StretchSettings settings{std::uint32_t(number("timeNumerator",1000000)),std::uint32_t(number("timeDenominator",1000000)),
        r.at("pitchMilliCents").get<std::int32_t>(),r.at("formantPreserved").get<bool>()};
    check(settings==canonicalStretchSettings(settings),"Noncanonical recovered duration");
    std::optional<StretchContext> context;
    if(r.at("contextEnabled").get<bool>())context=StretchContext{Frame(number("contextBefore",1000000000)),Frame(number("contextAfter",1000000000))};
    std::optional<WarpSettings> warp;
    if(r.contains("warp")){const auto input=Frame(number("frames",1000000000));warp=warp_codec::decode(r.at("warp"),input,stretchOutputFrames(input,settings),8*1024*1024,ledger);}
    auto p=prepareClipStretch(s,s.tracks[0].id,s.tracks[0].clips[0].id,settings,context,warp);
    check(minimal(s,p)==s,"Unrelated state in render selection");
    auto request=encodeStretchRenderRequest(p,op,saved,ledger);
    check(warp_codec::exact(r,parse(request.bytes())) && j.at("renderKey").get_ref<const std::string &>()==stretchRenderKey(p.source,p.anchor,ledger),"Recovered settings differ from render identity");
    file.verifyUnchanged();poll(stop);
    PayloadCharge payload("Retained render recovery",SIZE_MAX);payload.add(65536);payload.add(sessionPayloadBytes(s));
    if(p.anchor.warp)payload.add(warpPayloadBytes(*p.anchor.warp));
    if(p.expectedClip.stretch && p.expectedClip.stretch->warp)payload.add(warpPayloadBytes(*p.expectedClip.stretch->warp));
    StretchRecoveryEntry result{op};result.lease=ledger.reserve(payload.bytes());result.plan=std::move(p);
    if(current.id!=s.id || current.sampleRate!=s.sampleRate){result.status=StretchRecoveryStatus::Stale;return result;}
    const auto job=root/"media"/"derived"/op.str();
    if(!std::filesystem::exists(std::filesystem::symlink_status(job))){result.status=StretchRecoveryStatus::Incomplete;return result;}
    media_io::JobLease lock(job,false);
    if(lock.status()==media_io::LeaseStatus::Busy){result.status=StretchRecoveryStatus::Active;return result;}
    check(lock.status()==media_io::LeaseStatus::Held,"Render job ownership marker missing");
    if(!std::filesystem::exists(std::filesystem::symlink_status(job/"complete.json"))){result.status=StretchRecoveryStatus::Incomplete;return result;}
    auto binding=owned.open("media/derived/"+op.str()+"/selection.sha256",64,stop);
    check(binding.size()==64,"Invalid parent selection binding");std::array<char,64> captured{};binding.readAt(0,captured,stop);
    check(captured==file.digest(stop),"Original parent selection bytes changed");binding.verifyUnchanged();
    auto intentFile=owned.open("media/derived/"+op.str()+"/intent.json",stretchProtocolLimit(result.plan->anchor),stop);
    auto intentCredit=ledger.reserve(std::size_t(intentFile.size())*2);std::string intentBytes(std::size_t(intentFile.size()),'\0');
    for(std::size_t at=0;at<intentBytes.size();at+=65536){poll(stop);intentFile.readAt(at,{intentBytes.data()+at,std::min<std::size_t>(65536,intentBytes.size()-at)},stop);}
    auto encodedKey=encodeStretchRenderKey(result.plan->source,result.plan->anchor,ledger);auto intent=parse(encodedKey.bytes());
    intent["protocol"]=stretchProtocolFor(result.plan->anchor);intent["operation"]=op.str();intent["complete"]=false;
    intent["assetId"]=result.plan->source.id.str();intent["relative"]=result.plan->source.relativePath;intent["sourceFrames"]=result.plan->source.frames;
    check(warp_codec::exact(parse(intentBytes),intent),"Retained render intent differs from selection");intentFile.verifyUnchanged();
    auto verified=verifyOwnedClipStretch(root,*result.plan,op,saved,ledger,stop);file.verifyUnchanged();poll(stop);
    intentFile.verifyUnchanged();binding.verifyUnchanged();
    const auto &edit=verified.edit();
    const auto asset=std::find_if(current.assets.begin(),current.assets.end(),[&](const auto &v){return v.id==op;});
    if(asset!=current.assets.end())check(*asset==edit.rendered,"Render operation already names different media");
    const auto attached=[&](const auto &clips){
        return std::any_of(clips.begin(),clips.end(),[&](const auto &c){return c.assetId==op && c.stretch==edit.value;});
    };
    for(const auto &t:current.tracks){
        // An inactive arrangement still owns its rendered media. Do not offer
        // that retained operation as a new recovery or label it stale.
        if(attached(t.clips) || std::any_of(t.inactiveVersions.begin(),t.inactiveVersions.end(),
            [&](const auto &v){return attached(v.clips);})){
            result.status=StretchRecoveryStatus::Attached;return result;
        }
    }
    const auto track=std::find_if(current.tracks.begin(),current.tracks.end(),[&](const auto &v){return v.id==edit.track;});
    if(track==current.tracks.end()){result.status=StretchRecoveryStatus::Stale;return result;}
    const auto clip=std::find_if(track->clips.begin(),track->clips.end(),[&](const auto &v){return v.id==edit.clip;});
    if(clip==track->clips.end() || *clip!=edit.expected){result.status=StretchRecoveryStatus::Stale;return result;}
    auto currentAdmission=ledger.reserve(sessionPayloadBytes(current));
    const auto plan=prepareClipStretch(current,edit.track,edit.clip,settings,context,warp);
    if(plan.source!=edit.source || plan.anchor!=result.plan->anchor){result.status=StretchRecoveryStatus::Stale;return result;}
    result.verified=std::make_shared<const VerifiedClipStretch>(std::move(verified));result.status=StretchRecoveryStatus::Ready;return result;
}
}
std::string persistStretchSelection(const std::filesystem::path &root,const Session &s,const ClipStretchPlan &p,const Id &op,
    const StretchRenderPolicy &policyValue,ResourceLedger ledger,StretchRecoveryLimits l,std::stop_token stop){
    check(root.is_absolute() && root.native().size()<=4096 &&
        root.native().find(std::filesystem::path::value_type(0))==root.native().npos,
        "Render selection root must be an explicit absolute directory",ErrorCode::InvalidParameter);
    limits(l);poll(stop);auto work=ledger.reserve(codecCredit);auto bank=ledger.reserve(l.maximumManifestBytes*2);
    auto request=encodeStretchRenderRequest(p,op,policyValue,ledger);auto m=minimal(s,p);
    const auto recomputed=prepareClipStretch(m,p.trackId,p.expectedClip.id,p.anchor.settings,p.anchor.context,p.anchor.warp);
    check(recomputed.source==p.source && recomputed.anchor==p.anchor && recomputed.expectedClip==p.expectedClip,"Selection differs from prepared clip");
    auto policyObject=policyJson(policyValue);(void)policy(policyObject,l);
    Json j={{"format",format},{"operation",op.str()},{"selection",encodeProject(m,budget())},
        {"request",parse(request.bytes())},{"policy",policyObject},{"renderKey",stretchRenderKey(p.source,p.anchor,ledger)}};
    auto bytes=j.dump();check(bytes.size()<=l.maximumManifestBytes,"Render selection exceeds bank",ErrorCode::ResourceLimit);
    media_io::SampleHash hash;hash.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});const auto selectionDigest=hash.digest();
    media_io::plainDirectory(root);mkdir(root/"media");mkdir(root/"media"/"stretch-selections");
    const auto selections=root/"media"/"stretch-selections";
    const bool newLock=!std::filesystem::exists(std::filesystem::symlink_status(selections/"writer.lock"));
    media_io::JobLease publication(selections,newLock,!newLock);
    check(publication.status()==media_io::LeaseStatus::Held,"Render selection publication is busy",ErrorCode::Io);
    // Admission covers this immutable manifest, independent of project history.
    // Recovery's entry/total-byte limits apply per page. No directory inventory,
    // aggregate counter or persistent index is needed to publish one new leaf.
    // OS write/flush/publish failures retain the existing no-overwrite cleanup.
    poll(stop);
    const auto destination=root/utf8Path(relative(op)),temp=destination.parent_path()/("selection-"+Id::generate().str()+".partial");
    bool owned=false;
    try{media_io::File out(temp,true);owned=true;out.write(bytes);out.flush();out.close();poll(stop);media_io::publishMedia(temp,destination);owned=false;}
    catch(...){if(owned){std::error_code ignored;std::filesystem::remove(temp,ignored);}throw;}
    return selectionDigest;
}
void bindStretchSelection(const std::filesystem::path &root,const Id &op,std::string_view expected,
    ResourceLedger ledger,StretchRecoveryLimits l,std::stop_token stop){
    limits(l);poll(stop);check(expected.size()==64,"Invalid parent selection digest");auto credit=ledger.reserve(8192);
    ApprovedMediaRoot owned(root,ledger,{1});auto selection=owned.open(relative(op),l.maximumManifestBytes,stop);
    const auto actual=selection.digest(stop);check(std::string_view(actual.data(),actual.size())==expected,"Parent selection changed before rendering");
    const auto job=root/"media"/"derived"/op.str();media_io::plainDirectory(root/"media");media_io::plainDirectory(root/"media"/"derived");media_io::plainDirectory(job);
    const auto destination=job/"selection.sha256",temp=job/("selection-"+Id::generate().str()+".partial");bool created=false;
    try{media_io::File out(temp,true);created=true;out.write(expected);out.flush();out.close();selection.verifyUnchanged();poll(stop);media_io::publishMedia(temp,destination);created=false;}
    catch(...){if(created){std::error_code ignored;std::filesystem::remove(temp,ignored);}throw;}
}
StretchRecoveryEntry inspectStretchSelection(const std::filesystem::path &root,const Session &s,const Id &op,
    ResourceLedger ledger,StretchRecoveryLimits l,std::stop_token stop){
    try{return inspect(root,s,op,ledger,l,stop);}
    catch(const ProjectError &e){if(e.code()==ErrorCode::ResourceLimit || e.code()==ErrorCode::Canceled)throw;StretchRecoveryEntry result{op};result.lease=ledger.reserve(8192);result.error=e.code();result.diagnostic.assign(e.what(),std::min<std::size_t>(std::char_traits<char>::length(e.what()),1024));return result;}
    catch(const Json::exception &){StretchRecoveryEntry result{op};result.lease=ledger.reserve(8192);result.error=ErrorCode::InvalidState;return result;}
}
std::vector<StretchRecoveryEntry> inventoryStretchSelections(const std::filesystem::path &root,const Session &s,
    ResourceLedger ledger,StretchRecoveryLimits l,std::stop_token stop){
    limits(l);poll(stop);ApprovedMediaRoot owned(root,ledger,{1});
    const auto directory=root/"media"/"stretch-selections";
    if(!std::filesystem::exists(std::filesystem::symlink_status(directory)))return {};
    media_io::plainDirectory(root/"media");media_io::plainDirectory(directory);
    media_io::JobLease publication(directory,false);
    check(publication.status()==media_io::LeaseStatus::Held,"Render selection inventory is busy or unowned",ErrorCode::Io);
    ApprovedMediaRoot entries(directory,ledger,{1});
    auto work=ledger.reserve(l.maximumEntries*8192);std::vector<Id> ids;std::size_t count=0,total=0;
    for(const auto &entry:std::filesystem::directory_iterator(directory)){
        poll(stop);if(entry.path().filename()=="writer.lock")continue;
        check(++count<=l.maximumEntries,"Render inventory entry limit",ErrorCode::ResourceLimit);
        auto file=entries.openSelectedFilename(entry.path().filename(),l.maximumManifestBytes,stop);
        const auto name=entry.path().filename().u8string();const std::string token(reinterpret_cast<const char *>(name.data()),name.size());
        const auto n=file.size();file.verifyUnchanged();
        check(n<=l.maximumManifestBytes && n<=l.maximumTotalBytes-total,"Render inventory byte limit",ErrorCode::ResourceLimit);total+=std::size_t(n);
        if(token.size()==41 && token.substr(36)==".json"){
            try{ids.emplace_back(token.substr(0,36));}
            catch(const ProjectError &e){if(e.code()!=ErrorCode::InvalidId)throw;}
        }
    }
    std::sort(ids.begin(),ids.end(),[](const auto &a,const auto &b){return a.str()<b.str();});
    std::vector<StretchRecoveryEntry> result;result.reserve(ids.size());
    for(const auto &op:ids){poll(stop);result.push_back(inspectStretchSelection(root,s,op,ledger,l,stop));}return result;
}
struct StretchRecoveryScan::State {
    ResourceLease lease;
    ResourceLedger ledger;
    std::filesystem::path root,directory;
    std::shared_ptr<const Session> session;
    StretchRecoveryLimits bounds;
    std::unique_ptr<ApprovedMediaRoot> entries;
    ApprovedDirectoryStamp initial;
    std::filesystem::directory_iterator cursor;
    bool started=false,done=false,failed=false;
    State(const std::filesystem::path &r,const Session &s,ResourceLedger bank,StretchRecoveryLimits l)
        :lease(bank.reserve(65536)),ledger(bank),root(r),directory(r/"media"/"stretch-selections"),
         session(SessionSnapshots(bank).copy(s)),bounds(l) {}
    void unchanged() {
        media_io::plainDirectory(root/"media");media_io::plainDirectory(directory);
        ApprovedMediaRoot current(directory,ledger,{1});
        check(entries->directoryStamp()==initial && current.directoryStamp()==initial,
            "Render inventory changed; restart the scan");
    }
    StretchRecoveryPage page(std::stop_token stop) {
        check(!failed,"Render inventory cursor expired; restart the scan");poll(stop);
        ApprovedMediaRoot projectRoot(root,ledger,{1});
        StretchRecoveryPage result;
        if(done)return result;
        if(!started && !std::filesystem::exists(std::filesystem::symlink_status(directory))){done=true;return result;}
        media_io::plainDirectory(root/"media");media_io::plainDirectory(directory);
        // Shared only during this page. A publisher can proceed while the user
        // reviews the page; its directory change invalidates continuation.
        media_io::JobLease publication(directory,false);
        check(publication.status()==media_io::LeaseStatus::Held,"Render selection inventory is busy or unowned",ErrorCode::Io);
        if(!started){
            entries=std::make_unique<ApprovedMediaRoot>(directory,ledger,ApprovedMediaLimits{1});
            initial=entries->directoryStamp();cursor=std::filesystem::directory_iterator(directory);started=true;
        }
        unchanged();
        result.lease=ledger.reserve(bounds.maximumEntries*8192);
        std::vector<Id> ids;ids.reserve(bounds.maximumEntries);
        while(cursor!=std::filesystem::directory_iterator{}){
            poll(stop);const auto name=cursor->path().filename();
            if(name=="writer.lock"){++cursor;continue;}
            if(result.examined==bounds.maximumEntries)break;
            auto file=entries->openSelectedFilename(name,bounds.maximumManifestBytes,stop);
            const auto size=std::size_t(file.size());file.verifyUnchanged();
            if(size>bounds.maximumTotalBytes-result.manifestBytes)break;
            ++result.examined;result.manifestBytes+=size;
            const auto bytes=name.u8string();const std::string token(reinterpret_cast<const char *>(bytes.data()),bytes.size());
            if(token.size()==41 && token.substr(36)==".json"){
                try{ids.emplace_back(token.substr(0,36));}
                catch(const ProjectError &e){if(e.code()!=ErrorCode::InvalidId)throw;}
            }
            ++cursor;
        }
        std::sort(ids.begin(),ids.end(),[](const auto &a,const auto &b){return a.str()<b.str();});
        result.entries.reserve(ids.size());
        for(const auto &op:ids){poll(stop);unchanged();result.entries.push_back(inspectStretchSelection(root,*session,op,ledger,bounds,stop));}
        poll(stop);unchanged();result.hasMore=cursor!=std::filesystem::directory_iterator{};
        done=!result.hasMore;return result;
    }
};
StretchRecoveryScan::StretchRecoveryScan(const std::filesystem::path &root,const Session &s,ResourceLedger ledger,StretchRecoveryLimits l) {
    limits(l);check(root.is_absolute() && root.native().size()<=4096 &&
        root.native().find(std::filesystem::path::value_type(0))==root.native().npos,"Invalid render inventory root");
    state_=std::make_unique<State>(root,s,ledger,l);
}
StretchRecoveryScan::~StretchRecoveryScan()=default;
StretchRecoveryPage StretchRecoveryScan::next(std::stop_token stop) {
    try{return state_->page(stop);}
    catch(...){state_->failed=true;state_->cursor={};state_->entries.reset();throw;}
}
} // namespace soundcurrent::daw
