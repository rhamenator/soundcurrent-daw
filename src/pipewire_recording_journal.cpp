// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_recording_journal.hpp>
#include "media_io.hpp"
#include "original_recording_internal.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <unordered_set>
namespace soundcurrent::daw {
namespace {
using J=nlohmann::json;using media_io::require;
constexpr std::uint64_t maximumReceiptBytes=64ULL*1024*1024*1024;
void validate(const PipeWireRecordingSpec &s){require(s.generation && s.renderIdentity.endpoint && s.renderIdentity.stream && s.renderIdentity.generation &&
    s.renderRate>=8000 && s.renderRate<=384000 && s.renderChannels && s.renderChannels<=256 && s.maximumRenderFrames && s.maximumRenderFrames<=65536,
    "Invalid PipeWire recording declaration");}
std::uint64_t integer(const J &v){require(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>()>=0),"Invalid PipeWire journal integer");return v.get<std::uint64_t>();}
std::uint32_t small(const J &v){const auto n=integer(v);require(n<=UINT32_MAX,"PipeWire journal integer overflow");return std::uint32_t(n);}
std::string text(const J &v,std::size_t limit=128){require(v.is_string() && v.get_ref<const std::string &>().size()<=limit,"PipeWire journal text bound");return v.get<std::string>();}
std::string digest(const J &v){auto s=text(v);require(s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}),"Invalid PipeWire prefix digest");return s;}
void fields(const J &j,std::initializer_list<const char *> keys){require(j.is_object() && j.size()==keys.size(),"PipeWire journal fields mismatch");for(auto key:keys)require(j.contains(key),"PipeWire journal field missing");}
J identity(ClockStreamIdentity i){return {{"endpoint",i.endpoint},{"stream",i.stream},{"generation",i.generation}};}
ClockStreamIdentity identity(const J &j){fields(j,{"endpoint","stream","generation"});return {integer(j.at("endpoint")),integer(j.at("stream")),integer(j.at("generation"))};}
J declaration(const PipeWireRecordingSpec &s){return {{"identity",identity(s.renderIdentity)},{"rate",s.renderRate},{"channels",s.renderChannels},{"maximumFrames",s.maximumRenderFrames}};}
J manifest(const PipeWireRecordingPrefix &p,std::string_view rawJournal){return {
    {"format","soundcurrent-pipewire-original"},{"schemaMajor",1},{"schemaMinor",0},{"domain","posix-monotonic-ns"},
    {"run",p.declared.run.str()},{"generation",p.declared.generation},{"render",declaration(p.declared)},
    {"capture",{{"records",p.captureRecords},{"bytes",p.captureBytes},{"sha256",p.captureSha256},{"rawFrames",p.rawReceiptFrames}}},
    {"renderReceipts",{{"records",p.renderRecords},{"bytes",p.renderBytes},{"sha256",p.renderSha256},{"monitorQueued",p.monitorQueuedFrames},{"nativeQueued",p.nativeQueuedFrames}}},
    {"rawJournal",std::string(rawJournal)},{"phase",p.finalized?"finalized":"capturing"}};}
J parse(std::string_view bytes){try{
    unsigned events=0;std::vector<std::unordered_set<std::string>> keys;
    auto j=J::parse(bytes.begin(),bytes.end(),[&](int depth,J::parse_event_t event,J &v){require(depth<=4 && ++events<=256 && event!=J::parse_event_t::array_start,"PipeWire journal structure bound");
        if(event==J::parse_event_t::object_start)keys.emplace_back();else if(event==J::parse_event_t::key)require(!keys.empty() && v.get_ref<const std::string &>().size()<=64 && keys.back().insert(v.get<std::string>()).second,"Duplicate PipeWire journal key");else if(event==J::parse_event_t::object_end)keys.pop_back();return true;});
    fields(j,{"format","schemaMajor","schemaMinor","domain","run","generation","render","capture","renderReceipts","rawJournal","phase"});
    require(text(j.at("format"))=="soundcurrent-pipewire-original" && integer(j.at("schemaMajor"))==1 && !integer(j.at("schemaMinor")) && text(j.at("domain"))=="posix-monotonic-ns","Unsupported PipeWire journal");return j;
}catch(const J::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed PipeWire journal");}}
std::string framed(const PipeWireReceipt &r){auto bytes=encodePipeWireReceipt(r);std::string out(4,'\0');const auto n=std::uint32_t(bytes.size());for(unsigned k=0;k<4;++k)out[k]=char((n>>(k*8))&255);out+=bytes;return out;}
std::string readRecord(media_io::File &file,std::uint64_t left){require(left>=4,"Truncated native receipt length");std::array<std::byte,4> head{};file.readExact(head);std::uint32_t n=0;
    for(unsigned k=0;k<4;++k)n|=std::uint32_t(std::to_integer<unsigned>(head[k]))<<(k*8);
    require(n && n<=4096 && n<=left-4,"Native receipt record extent bound");std::string bytes(n,'\0');file.readExact({reinterpret_cast<std::byte *>(bytes.data()),bytes.size()});
    return std::string(reinterpret_cast<const char *>(head.data()),head.size())+bytes;}
void add(std::uint64_t &v,std::uint64_t n){require(n<=UINT64_MAX-v,"Native receipt counter overflow");v+=n;}
void captureGeometry(const PipeWireCaptureCycle &c,const PipeWireRecordingSpec &s){require(c.channels==s.raw.layout.channels && c.requestedFrames<=s.raw.maximumPacketFrames && c.rawFrames<=c.requestedFrames,"Native capture declaration mismatch");
    if(c.rawFrames)require(c.rawFrames==c.requestedFrames && c.original.duration==c.rawFrames && c.original.rateNumerator==1 && c.original.rateDenominator==s.raw.sampleRate &&
        c.original.id && c.original.monotonicNs>=100 && !c.original.xrun && !c.original.discontinuity,"Copied raw/native capture facts mismatch");}
void renderGeometry(const PipeWireRenderCycle &c,const PipeWireRecordingSpec &s){require(c.channels==s.renderChannels && c.requestedFrames<=s.maximumRenderFrames,"Native render declaration mismatch");}
}
struct PipeWireRecordingJournal::State {
    PipeWireRecordingSpec spec;PipeWireJournalOptions options;ResourceLease resources;OriginalCaptureWriter raw;
    std::unique_ptr<media_io::JobLease> lease;
    std::unique_ptr<media_io::File> capture,render;
    media_io::SampleHash captureHash,renderHash;
    PipeWireRecordingPrefix prefix;
    std::uint64_t sinceCheckpoint=0;bool failed=false,finalized=false;
    static PipeWireRecordingSpec admit(const std::filesystem::path &root,PipeWireRecordingSpec s){validate(s);const auto p=ProjectStore(root).load();
        require(p.id==s.raw.project && std::any_of(p.tracks.begin(),p.tracks.end(),[&](const auto &track){return track.id==s.raw.track;}),
                "Native recording project/track identity mismatch");return s;}
    static ResourceLease reserve(const std::filesystem::path &root,const PipeWireJournalOptions &o){PayloadCharge c("PipeWire journal workspace",SIZE_MAX);c.add(192*1024);c.add(root.native().size(),8*sizeof(std::filesystem::path::value_type));return o.raw.resources?o.raw.resources->reserve(c.bytes()):ResourceLease{};}
    State(std::filesystem::path root,PipeWireRecordingSpec s,PipeWireJournalOptions o):spec(admit(root,std::move(s))),options(std::move(o)),resources(reserve(root,options)),raw(root,spec.raw,options.raw){
        require(options.checkpointRecords && options.checkpointRecords<=1000000,"Native receipt checkpoint policy invalid");
        lease=std::make_unique<media_io::JobLease>(raw.jobDirectory(),true,false,media_io::JobLeaseKind::PipeWireReceipts);
        capture=std::make_unique<media_io::File>(raw.jobDirectory()/"capture-cycles.bin",true);render=std::make_unique<media_io::File>(raw.jobDirectory()/"render-cycles.bin",true);
        prefix.declared=spec;commit(false);at(PipeWireJournalBoundary::PendingPublished);
    }
    void at(PipeWireJournalBoundary b){if(options.boundary)options.boundary(b);}
    void active(){require(!failed && !finalized,"Native journal writer inactive");}
    void commit(bool final){raw.checkpoint();at(PipeWireJournalBoundary::RawCheckpoint);capture->flush();render->flush();at(PipeWireJournalBoundary::ReceiptsFlushed);
        publish(final,raw.committedJournal());}
    void publish(bool final,const std::string &rawJournal){prefix.captureSha256=captureHash.digest();prefix.renderSha256=renderHash.digest();prefix.finalized=final;
        at(PipeWireJournalBoundary::BeforeCheckpoint);media_io::publishJournal(raw.jobDirectory()/"pipewire-run.json",manifest(prefix,rawJournal).dump(2)+"\n");sinceCheckpoint=0;at(final?PipeWireJournalBoundary::FinalPublished:PipeWireJournalBoundary::CheckpointPublished);}
    void fail() noexcept{failed=true;capture.reset();render.reset();lease.reset();}
};
PipeWireRecordingJournal::PipeWireRecordingJournal(std::filesystem::path root,PipeWireRecordingSpec s,PipeWireJournalOptions o):state_(std::make_unique<State>(std::move(root),std::move(s),std::move(o))){}
PipeWireRecordingJournal::~PipeWireRecordingJournal()=default;
const std::filesystem::path &PipeWireRecordingJournal::jobDirectory() const noexcept{return state_->raw.jobDirectory();}
void PipeWireRecordingJournal::appendCapture(const PipeWireReceiptRecord<PipeWireCaptureCycle> &r,const std::optional<CaptureBankView> &view,bool deferCheckpoint){auto &s=*state_;s.active();captureGeometry(r.original,s.spec);
    require(r.generation==s.spec.generation && r.sequence==s.prefix.captureRecords && bool(view)==bool(r.original.rawFrames),"Native capture receipt/view sequence mismatch");
    if(view){const auto &t=view->token.original;require(t.identity==s.spec.raw.identity && t.frames==r.original.rawFrames && t.position.value==r.original.original.position &&
        t.qpc.value==r.original.original.monotonicNs/100 && !t.flags,"Native receipt differs from raw packet stamp");}
    const auto bytes=framed(r);require(bytes.size()<=maximumReceiptBytes-s.prefix.captureBytes,"Native receipt file byte limit");
    try{if(view){s.raw.append(*view);s.at(PipeWireJournalBoundary::RawAppended);}s.capture->write(bytes);s.at(PipeWireJournalBoundary::ReceiptWritten);
        s.captureHash.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});add(s.prefix.captureBytes,bytes.size());add(s.prefix.captureRecords,1);add(s.prefix.rawReceiptFrames,r.original.rawFrames);
        ++s.sinceCheckpoint;if(!deferCheckpoint && s.sinceCheckpoint>=s.options.checkpointRecords)s.commit(false);
    }catch(...){s.fail();throw;}}
void PipeWireRecordingJournal::appendRender(const PipeWireReceiptRecord<PipeWireRenderCycle> &r){auto &s=*state_;s.active();renderGeometry(r.original,s.spec);
    require(r.generation==s.spec.generation && r.sequence==s.prefix.renderRecords,"Native render sequence mismatch");const auto bytes=framed(r);require(bytes.size()<=maximumReceiptBytes-s.prefix.renderBytes,"Native receipt file byte limit");
    try{s.render->write(bytes);s.at(PipeWireJournalBoundary::ReceiptWritten);s.renderHash.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});add(s.prefix.renderRecords,1);add(s.prefix.renderBytes,bytes.size());
        if(r.original.allQueued){add(s.prefix.monitorQueuedFrames,r.original.monitorFrames);add(s.prefix.nativeQueuedFrames,r.original.requestedFrames);}
        if(++s.sinceCheckpoint>=s.options.checkpointRecords)s.commit(false);
    }catch(...){s.fail();throw;}}
void PipeWireRecordingJournal::checkpoint(){auto &s=*state_;s.active();try{s.commit(false);}catch(...){s.fail();throw;}}
void PipeWireRecordingJournal::checkpointIfDue(){auto &s=*state_;s.active();try{if(s.sinceCheckpoint>=s.options.checkpointRecords)s.commit(false);}catch(...){s.fail();throw;}}
void PipeWireRecordingJournal::finalize(const PreparedCaptureFanout &f,const PreparedPipeWireReceiptQueues &q){auto &s=*state_;s.active();require(q.captureFinishedAndEmpty() && q.renderFinishedAndEmpty() && !q.consumerLost(),"Native receipt owners not finished/drained");
    try{s.commit(false);s.prefix.raw=s.raw.finalize(f);s.at(PipeWireJournalBoundary::RawFinalized);require(s.prefix.raw.committedFrames==s.prefix.rawReceiptFrames,"Raw/native final prefix differs");
        s.capture->flush();s.render->flush();s.publish(true,s.raw.committedJournal());s.capture->close();s.render->close();s.finalized=true;s.lease.reset();
    }catch(...){s.fail();throw;}}
PipeWireRecordingPrefix inspectPipeWireRecording(const std::filesystem::path &job,const std::optional<PipeWireRecordingSpec> &expected,std::optional<ResourceLedger> resources,const std::function<void()> &beforeRead){
    const auto at=[&]{if(beforeRead)beforeRead();};at();media_io::plainDirectory(job);
    media_io::JobLease native(job,false,true,media_io::JobLeaseKind::PipeWireReceipts);require(native.status()==media_io::LeaseStatus::Held,"Native writer active or lease missing");
    media_io::JobLease raw(job,false,true);require(raw.status()==media_io::LeaseStatus::Held,"Raw writer active or lease missing");
    PayloadCharge charge("Native receipt inspection",SIZE_MAX);charge.add(192*1024);auto reservation=resources?resources->reserve(charge.bytes()):ResourceLease{};
    try{const auto j=parse(media_io::readJournal(job/"pipewire-run.json"));PipeWireRecordingPrefix p;auto &s=p.declared;
        s.run=Id(text(j.at("run")));s.generation=integer(j.at("generation"));const auto &render=j.at("render");fields(render,{"identity","rate","channels","maximumFrames"});
        s.renderIdentity=identity(render.at("identity"));s.renderRate=small(render.at("rate"));s.renderChannels=small(render.at("channels"));s.maximumRenderFrames=small(render.at("maximumFrames"));
        const auto rawJournal=text(j.at("rawJournal"),16384);p.raw=detail::inspectOriginalRecordingHeld(job,raw,expected?std::optional(expected->raw):std::nullopt,beforeRead,resources,rawJournal);s.raw=p.raw.spec;validate(s);
        require(!expected || *expected==s,"Native recording expected context mismatch");const auto phase=text(j.at("phase"));require(phase=="capturing" || phase=="finalized","Native recording phase unknown");p.finalized=phase=="finalized";
        require(!p.finalized || p.raw.finalized,"Native finalized checkpoint lacks finalized raw prefix");
        const auto &c=j.at("capture"),&r=j.at("renderReceipts");fields(c,{"records","bytes","sha256","rawFrames"});fields(r,{"records","bytes","sha256","monitorQueued","nativeQueued"});
        p.captureRecords=integer(c.at("records"));p.captureBytes=integer(c.at("bytes"));p.captureSha256=digest(c.at("sha256"));p.rawReceiptFrames=integer(c.at("rawFrames"));
        p.renderRecords=integer(r.at("records"));p.renderBytes=integer(r.at("bytes"));p.renderSha256=digest(r.at("sha256"));p.monitorQueuedFrames=integer(r.at("monitorQueued"));p.nativeQueuedFrames=integer(r.at("nativeQueued"));
        require(p.captureBytes<=maximumReceiptBytes && p.renderBytes<=maximumReceiptBytes && p.captureRecords<=p.captureBytes/4 && p.renderRecords<=p.renderBytes/4 && p.rawReceiptFrames==p.raw.committedFrames,"Native receipt extent limits/binding");
        media_io::File captures(job/"capture-cycles.bin",false),renders(job/"render-cycles.bin",false),stamps(job/"packets.bin",false);media_io::SampleHash ch,rh;
        std::uint64_t cb=0,rb=0,rawFrames=0,rawPackets=0,monitorFrames=0,nativeFrames=0;
        for(std::uint64_t n=0;n<p.captureRecords;++n){at();const auto bytes=readRecord(captures,p.captureBytes-cb);const auto decoded=decodePipeWireReceipt(std::string_view(bytes).substr(4));
            const auto *record=std::get_if<PipeWireReceiptRecord<PipeWireCaptureCycle>>(&decoded);require(record && record->generation==s.generation && record->sequence==n,"Native capture role/sequence mismatch");captureGeometry(record->original,s);
            if(record->original.rawFrames){std::array<std::byte,80> data{};stamps.readExact(data);const auto t=detail::decodeOriginalPacketRecord(data,rawFrames);
                require(t.sequence==s.raw.initialPacketSequence+rawPackets && t.original.identity==s.raw.identity && t.original.frames==record->original.rawFrames &&
                    t.original.position.value==record->original.original.position && t.original.qpc.value==record->original.original.monotonicNs/100 && !t.original.flags,"Native/raw packet provenance mismatch");
                add(rawFrames,record->original.rawFrames);++rawPackets;}
            add(cb,bytes.size());ch.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});}
        for(std::uint64_t n=0;n<p.renderRecords;++n){at();const auto bytes=readRecord(renders,p.renderBytes-rb);const auto decoded=decodePipeWireReceipt(std::string_view(bytes).substr(4));
            const auto *record=std::get_if<PipeWireReceiptRecord<PipeWireRenderCycle>>(&decoded);require(record && record->generation==s.generation && record->sequence==n,"Native render role/sequence mismatch");renderGeometry(record->original,s);
            if(record->original.allQueued){add(monitorFrames,record->original.monitorFrames);add(nativeFrames,record->original.requestedFrames);
                if(record->original.finalContent && !p.completion)p.completion=*record;
            }add(rb,bytes.size());rh.updateBytes({reinterpret_cast<const std::byte *>(bytes.data()),bytes.size()});}
        require(cb==p.captureBytes && rb==p.renderBytes && ch.digest()==p.captureSha256 && rh.digest()==p.renderSha256 && rawFrames==p.raw.committedFrames && rawPackets==p.raw.committedPackets &&
            monitorFrames==p.monitorQueuedFrames && nativeFrames==p.nativeQueuedFrames,"Native/raw common prefix digest/count mismatch");
        if(p.finalized)require(captures.readBounded(1).empty() && renders.readBounded(1).empty(),"Finalized native receipt file has extra data");
        at();return p;
    }catch(const J::exception &){throw ProjectError(ErrorCode::InvalidState,"Malformed native receipt checkpoint");}
}
} // namespace soundcurrent::daw
