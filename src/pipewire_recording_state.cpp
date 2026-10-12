// SPDX-License-Identifier: GPL-3.0-only
#include "pipewire_state_codec.hpp"
namespace soundcurrent::daw {
namespace {
using namespace pw_state;
constexpr std::size_t maxArtifact=4*1024*1024;
J artifact(const std::filesystem::path &job,std::string_view type,std::string_view bytes,const PipeWireStateOptions &o){require(bytes.size()<=maxArtifact,"Native state artifact bound");
    const auto name="pw-"+std::string(type)+"-"+Id::generate().str()+".json";const auto partial=job/(name+".partial");bool owned=false;
    try{media_io::File file(partial,true);owned=true;file.write(bytes);file.flush();file.close();if(o.boundary)o.boundary(PipeWireStateBoundary::ArtifactFlushed);
        media_io::publishMedia(partial,job/name);owned=false;if(o.boundary)o.boundary(PipeWireStateBoundary::ArtifactPublished);
    }catch(...){if(owned){std::error_code e;std::filesystem::remove(partial,e);}throw;}
    return {{"file",name},{"bytes",bytes.size()},{"sha256",hash(bytes)}};
}
J readArtifact(const std::filesystem::path &job,const J &ref,std::string_view type,const std::function<void()> &beforeRead){fields(ref,{"file","bytes","sha256"});const auto name=text(ref.at("file"),128);const auto prefix="pw-"+std::string(type)+"-";
    require(name.starts_with(prefix) && name.ends_with(".json") && name.size()==prefix.size()+36+5,"Invalid native state artifact name");Id id(name.substr(prefix.size(),36));(void)id;
    const auto bytes=u(ref.at("bytes"));require(bytes<=maxArtifact,"Native state artifact extent bound");if(beforeRead)beforeRead();media_io::File file(job/name,false);std::string raw(std::size_t(bytes),'\0');
    for(std::size_t at=0;at<raw.size();){if(beforeRead)beforeRead();const auto count=std::min<std::size_t>(65536,raw.size()-at);file.readExact({reinterpret_cast<std::byte *>(raw.data()+at),count});at+=count;}
    require(file.readBounded(1).empty(),"Native state artifact trailing bytes");
    require(raw.size()==bytes && hash(raw)==text(ref.at("sha256"),64),"Native state artifact checksum/extent mismatch");return parse(raw);
}
J manifest(const PipeWireRecordingSpec &s,const J &intent,const J &outcome,const J &prefix){return {{"format","soundcurrent-pipewire-owner"},{"major",1},{"minor",0},{"domain","posix-monotonic-ns"},{"run",s.run.str()},{"generation",s.generation},{"intent",intent},{"outcome",outcome},{"prefix",prefix}};}
ResourceLease reserve(const std::optional<ResourceLedger> &r){PayloadCharge c("Native durable state workspace",SIZE_MAX);c.add(24*1024*1024);return r?r->reserve(c.bytes()):ResourceLease{};}
}
struct PipeWireRecordingState::State {
    ResourceLease resources;std::filesystem::path job;PipeWireRecordingIntent intent;PipeWireStateOptions options;
    media_io::JobLease lease;pw_state::J intentRef;bool terminal=false,failed=false;
    State(std::filesystem::path j,PipeWireRecordingIntent i,PipeWireStateOptions o):resources(reserve(o.resources)),job(std::move(j)),intent(std::move(i)),options(std::move(o)),lease(job,true,false,media_io::JobLeaseKind::PipeWireState){
        const auto validated=pw_state::decodeIntent(pw_state::parse(pw_state::encodeIntent(intent).dump()),intent.expected);(void)validated;publishIntent();}
    void publish(const pw_state::J &outcome,const pw_state::J &prefix){if(options.boundary)options.boundary(PipeWireStateBoundary::BeforeManifest);
        media_io::publishJournal(job/"pipewire-owner.json",manifest(intent.expected,intentRef,outcome,prefix).dump(2)+"\n");
        if(options.boundary)options.boundary(PipeWireStateBoundary::ManifestPublished);}
    void publishIntent(){intentRef=artifact(job,"intent",pw_state::encodeIntent(intent).dump(),options);publish(nullptr,nullptr);}
};
PipeWireRecordingState::PipeWireRecordingState(std::filesystem::path job,PipeWireRecordingIntent i,PipeWireStateOptions o):state_(std::make_unique<State>(std::move(job),std::move(i),std::move(o))){}
PipeWireRecordingState::~PipeWireRecordingState()=default;
void PipeWireRecordingState::select(std::vector<ChannelPortIntent> inputs,std::vector<ChannelPortIntent> outputs){auto &s=*state_;pw_state::require(!s.terminal && !s.failed,"Native durable state inactive");
    auto next=s.intent;next.inputs=std::move(inputs);next.outputs=std::move(outputs);pw_state::decodeIntent(pw_state::parse(pw_state::encodeIntent(next).dump()),next.expected);
    try{s.intent=std::move(next);s.publishIntent();}catch(...){s.failed=true;throw;}}
void PipeWireRecordingState::finalize(const PipeWireRecordingPrefix &prefix,const PipeWireJoinedOutcome &outcome){auto &s=*state_;pw_state::require(!s.terminal && !s.failed,"Native terminal state already published/failed");
    const auto encoded=pw_state::encodeOutcome(outcome);const auto decoded=pw_state::decodeOutcome(pw_state::parse(encoded.dump()));pw_state::validateOutcome(s.intent,prefix,decoded);
    try{const auto ref=artifact(s.job,"outcome",encoded.dump(),s.options);s.publish(ref,pw_state::prefixBinding(prefix));s.terminal=true;}catch(...){s.failed=true;throw;}}
PipeWireStoredRecording inspectPipeWireRecordingState(const std::filesystem::path &job,const std::optional<PipeWireRecordingSpec> &expected,std::optional<ResourceLedger> resources,const std::function<void()> &beforeRead){
    using namespace pw_state;if(beforeRead)beforeRead();media_io::plainDirectory(job);media_io::JobLease lease(job,false,true,media_io::JobLeaseKind::PipeWireState);require(lease.status()==media_io::LeaseStatus::Held,"Native state writer active or missing");auto credit=reserve(resources);
    try{const auto j=parse(media_io::readJournal(job/"pipewire-owner.json"));fields(j,{"format","major","minor","domain","run","generation","intent","outcome","prefix"});
        require(text(j.at("format"))=="soundcurrent-pipewire-owner" && u(j.at("major"))==1 && !u(j.at("minor")) && text(j.at("domain"))=="posix-monotonic-ns","Unsupported native durable state schema");
        PipeWireStoredRecording r;r.prefix=inspectPipeWireRecording(job,expected,resources,beforeRead);require(Id(text(j.at("run")))==r.prefix.declared.run && u(j.at("generation"))==r.prefix.declared.generation,"Native state run identity mismatch");
        r.intent=decodeIntent(readArtifact(job,j.at("intent"),"intent",beforeRead),r.prefix.declared);
        require(j.at("outcome").is_null()==j.at("prefix").is_null(),"Native outcome/checkpoint publication mismatch");
        if(!j.at("outcome").is_null()){require(j.at("prefix")==prefixBinding(r.prefix),"Native terminal bound prefix differs from paired checkpoint");r.outcome=decodeOutcome(readArtifact(job,j.at("outcome"),"outcome",beforeRead));validateOutcome(r.intent,r.prefix,*r.outcome);r.terminal=true;}
        if(beforeRead)beforeRead();
        return r;
    }catch(const J::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed native durable state");}
}
} // namespace soundcurrent::daw
