// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/original_recording.hpp>
#include "media_io.hpp"
#include "original_recording_internal.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace soundcurrent::daw {
namespace {
using Json = nlohmann::json;
using media_io::require;
constexpr std::size_t recordBytes = 80;
using Record = std::array<std::byte, recordBytes>;
constexpr std::array<std::byte,8> magic{std::byte{'S'},std::byte{'C'},std::byte{'R'},std::byte{'P'},std::byte{1}};
void validate(const OriginalRecordingSpec &s) {
    require(s.identity.endpoint && s.identity.stream && s.identity.generation && s.sampleRate >= 8000 &&
            s.sampleRate <= 384000 && s.layout.channels && s.layout.channels <= 256 &&
            ((s.layout.kind == LayoutKind::Mono && s.layout.channels == 1) ||
             (s.layout.kind == LayoutKind::Stereo && s.layout.channels == 2) || s.layout.kind == LayoutKind::Discrete) &&
            s.maximumPacketFrames && s.maximumPacketFrames <= 65536 && s.initialPacketSequence != UINT64_MAX &&
            s.declaredInputLatencyFrames >= 0 && s.declaredInputLatencyFrames <= Frame(s.sampleRate) * 60,
            "Invalid original recording context");
}
Json context(const OriginalRecordingSpec &s) {
    return {{"project",s.project.str()},{"track",s.track.str()},{"asset",s.asset.str()},
            {"endpoint",s.identity.endpoint},{"stream",s.identity.stream},{"generation",s.identity.generation},
            {"layout",std::uint32_t(s.layout.kind)},{"channels",s.layout.channels},{"sampleRate",s.sampleRate},
            {"maximumPacketFrames",s.maximumPacketFrames},{"initialPacketSequence",s.initialPacketSequence},
            {"declaredInputLatencyFrames",s.declaredInputLatencyFrames}};
}
std::uint64_t u64(const Json &v) {
    require(v.is_number_integer() && (v.is_number_unsigned() || v.get<std::int64_t>() >= 0), "Invalid original unsigned integer");
    return v.get<std::uint64_t>();
}
std::uint32_t u32(const Json &v) {
    const auto n = u64(v); require(n <= UINT32_MAX, "Original integer exceeds uint32"); return std::uint32_t(n);
}
std::string text(const Json &v) {
    require(v.is_string() && v.get_ref<const std::string &>().size() <= 128, "Invalid original recording text");
    return v.get<std::string>();
}
std::string digest(const Json &v) {
    const auto s = text(v);
    require(s.size() == 64 && std::all_of(s.begin(),s.end(),[](char c){return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');}),
            "Invalid original prefix digest"); return s;
}
Json stampJson(const CaptureBankStamp &p) {
    return {{"endpoint",p.identity.endpoint},{"stream",p.identity.stream},{"generation",p.identity.generation},
            {"position",p.position.value},{"qpc100ns",p.qpc.value},{"frames",p.frames},{"flags",p.flags}};
}
CaptureBankStamp stampFrom(const Json &j) {
    require(j.is_object() && j.size() == 7, "Invalid original fault stamp");
    return {{u64(j.at("endpoint")),u64(j.at("stream")),u64(j.at("generation"))},
            {u64(j.at("position"))},{u64(j.at("qpc100ns"))},u32(j.at("frames")),u32(j.at("flags"))};
}
Json faultJson(const std::optional<CaptureFanoutFault> &f) {
    if (!f) return nullptr;
    return {{"side",std::uint32_t(f->side)},{"cause",std::uint32_t(f->cause)},
            {"attempted",f->attempted ? stampJson(*f->attempted) : Json(nullptr)},
            {"rawAccepted",f->rawPacketAccepted},{"monitorAccepted",f->monitorPacketAccepted}};
}
void validateFault(const OriginalRecordingPrefix &p) {
    if (!p.finalized) { require(!p.endStatus && !p.endFault, "Capturing original journal has final outcome"); return; }
    require(p.endStatus && *p.endStatus != CaptureFanoutStatus::Running, "Missing final original outcome");
    const bool fault = *p.endStatus == CaptureFanoutStatus::RawFault || *p.endStatus == CaptureFanoutStatus::MonitorFault;
    require(bool(p.endFault) == fault, "Original end fault/status mismatch");
    if (!fault) return;
    const auto &f = *p.endFault;
    require(f.cause >= CaptureBankStatus::ConsumerFailed && f.cause <= CaptureBankStatus::InternalQueueError &&
            ((f.side == CaptureFanoutSide::Raw && *p.endStatus == CaptureFanoutStatus::RawFault) ||
             (f.side == CaptureFanoutSide::Monitor && *p.endStatus == CaptureFanoutStatus::MonitorFault)) &&
            !f.monitorPacketAccepted && (!f.rawPacketAccepted || (f.side == CaptureFanoutSide::Monitor && f.attempted)) &&
            (f.attempted || f.cause == CaptureBankStatus::ConsumerFailed), "Invalid original fault disposition");
    // An attempted invalid/new-identity packet is fault evidence, not admitted PCM.
}
Json journal(const OriginalRecordingPrefix &p) {
    validateFault(p);
    return {{"format","soundcurrent-original-recording"},{"schemaMajor",1},{"schemaMinor",0},
            {"floatEncoding","ieee754-f32-le"},{"captureUnit","native-frames"},{"qpcUnit","100ns"},
            {"context",context(p.spec)},{"committedFrames",p.committedFrames},{"committedPackets",p.committedPackets},
            {"finitePrefixFrames",p.finitePrefixFrames},{"sampleSha256",p.sampleSha256},{"stampSha256",p.stampSha256},
            {"phase",p.finalized ? "finalized" : "capturing"},
            {"endStatus",p.endStatus ? Json(std::uint32_t(*p.endStatus)) : Json(nullptr)},{"endFault",faultJson(p.endFault)}};
}
OriginalRecordingPrefix decode(std::string_view bytes) {
    try {
        std::vector<std::unordered_set<std::string>> keys;
        unsigned events=0,keyCount=0;
        const auto j = Json::parse(bytes.begin(),bytes.end(),[&](int depth,Json::parse_event_t e,Json &v) {
            require(depth <= 5 && ++events <= 256 && e != Json::parse_event_t::array_start, "Original journal structure limit");
            if (e == Json::parse_event_t::object_start) keys.emplace_back();
            else if (e == Json::parse_event_t::key)
                require(++keyCount <= 64 && v.get_ref<const std::string &>().size() <= 64 &&
                        !keys.empty() && keys.back().insert(v.get<std::string>()).second, "Duplicate/oversized original journal key");
            else if (e == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        require(j.is_object() && j.size() == 15 && text(j.at("format")) == "soundcurrent-original-recording" &&
                u64(j.at("schemaMajor")) == 1 && u64(j.at("schemaMinor")) == 0 &&
                text(j.at("floatEncoding")) == "ieee754-f32-le" && text(j.at("captureUnit")) == "native-frames" &&
                text(j.at("qpcUnit")) == "100ns", "Unsupported original recording journal");
        OriginalRecordingPrefix p; auto &s = p.spec; const auto &c = j.at("context");
        require(c.is_object() && c.size() == 12, "Invalid original context fields");
        s.project=Id(text(c.at("project")));s.track=Id(text(c.at("track")));s.asset=Id(text(c.at("asset")));
        s.identity={u64(c.at("endpoint")),u64(c.at("stream")),u64(c.at("generation"))};
        const auto kind = u32(c.at("layout")); require(kind <= std::uint32_t(LayoutKind::Discrete), "Invalid original layout kind");
        s.layout={LayoutKind(kind),u32(c.at("channels"))};s.sampleRate=u32(c.at("sampleRate"));
        s.maximumPacketFrames=u32(c.at("maximumPacketFrames"));s.initialPacketSequence=u64(c.at("initialPacketSequence"));
        const auto delay=u64(c.at("declaredInputLatencyFrames"));require(delay <= std::uint64_t(INT64_MAX),"Original latency overflow");
        s.declaredInputLatencyFrames=Frame(delay);validate(s);
        p.committedFrames=u64(j.at("committedFrames"));p.committedPackets=u64(j.at("committedPackets"));
        p.finitePrefixFrames=u64(j.at("finitePrefixFrames"));
        require(p.committedFrames <= std::uint64_t(INT64_MAX) && p.committedPackets <= p.committedFrames &&
                ((p.committedPackets == 0) == (p.committedFrames == 0)) && p.finitePrefixFrames <= p.committedFrames &&
                s.initialPacketSequence <= UINT64_MAX-p.committedPackets, "Invalid original prefix extent");
        p.sampleSha256=digest(j.at("sampleSha256"));p.stampSha256=digest(j.at("stampSha256"));
        const auto phase=text(j.at("phase"));require(phase == "capturing" || phase == "finalized", "Invalid original phase");
        p.finalized=phase == "finalized";
        if (!j.at("endStatus").is_null()) {
            const auto n=u32(j.at("endStatus"));require(n <= std::uint32_t(CaptureFanoutStatus::MonitorFault), "Invalid original end status");
            p.endStatus=CaptureFanoutStatus(n);
        }
        const auto &f=j.at("endFault");
        if (!f.is_null()) {
            require(f.is_object() && f.size() == 5 && f.at("rawAccepted").is_boolean() && f.at("monitorAccepted").is_boolean(), "Invalid original end fault");
            const auto side=u32(f.at("side")),cause=u32(f.at("cause"));
            require(side <= std::uint32_t(CaptureFanoutSide::Monitor) && cause <= std::uint32_t(CaptureBankStatus::InternalQueueError), "Original fault enum overflow");
            p.endFault=CaptureFanoutFault{CaptureFanoutSide(side),CaptureBankStatus(cause),
                f.at("attempted").is_null() ? std::optional<CaptureBankStamp>{} : stampFrom(f.at("attempted")),
                f.at("rawAccepted").get<bool>(),f.at("monitorAccepted").get<bool>()};
        }
        validateFault(p);return p;
    } catch (const Json::exception &) { throw ProjectError(ErrorCode::InvalidState,"Malformed original recording journal"); }
}
void put(Record &r,std::size_t at,std::uint64_t v,unsigned bytes=8) {
    for(unsigned n=0;n<bytes;++n)r[at+n]=std::byte((v>>(8*n)) & 255);
}
std::uint64_t get(const Record &r,std::size_t at,unsigned bytes=8) {
    std::uint64_t v=0;for(unsigned n=0;n<bytes;++n)v|=std::uint64_t(std::to_integer<unsigned>(r[at+n]))<<(8*n);return v;
}
Record record(const CaptureBankToken &t,std::uint64_t offset) {
    Record r{};std::copy(magic.begin(),magic.end(),r.begin());
    put(r,8,t.sequence);put(r,16,offset);put(r,24,t.original.position.value);put(r,32,t.original.qpc.value);
    put(r,40,t.original.frames,4);put(r,44,t.original.flags,4);put(r,48,t.original.identity.endpoint);
    put(r,56,t.original.identity.stream);put(r,64,t.original.identity.generation); // Final8 bytes reserved zero.
    return r;
}
CaptureBankToken token(const Record &r,std::uint64_t offset) {
    require(std::equal(magic.begin(),magic.end(),r.begin()) && !get(r,72) && get(r,16) == offset,"Invalid original packet record");
    return {{{get(r,48),get(r,56),get(r,64)},{get(r,24)},{get(r,32)},std::uint32_t(get(r,40,4)),std::uint32_t(get(r,44,4))},get(r,8),0};
}
void validatePacket(const OriginalRecordingSpec &s,const CaptureBankToken &t,std::uint64_t offset,
                    std::uint64_t packets,const std::optional<CaptureBankStamp> &last) {
    const auto &p=t.original;
    require(t.sequence == s.initialPacketSequence+packets && t.sequence != UINT64_MAX && p.identity == s.identity &&
            p.frames && p.frames <= s.maximumPacketFrames && p.qpc.value &&
            !(p.flags & ~(bankSilent|bankDiscontinuity)) && p.position.value <= UINT64_MAX-p.frames &&
            offset <= std::uint64_t(INT64_MAX)-p.frames &&
            (!last || (!(p.flags & bankDiscontinuity) && p.position.value == last->position.value+last->frames && p.qpc.value > last->qpc.value)),
            "Original packet identity/sequence/continuity/extent mismatch");
}
std::uint64_t finitePrefix(std::span<const float> samples,unsigned channels,std::uint64_t before,std::uint64_t prior) {
    if(prior != before)return prior;
    for(std::size_t n=0;n<samples.size();++n)if(!std::isfinite(samples[n]))return before+n/channels;
    return before+samples.size()/channels;
}
void validateSilent(std::span<const float> samples,std::uint32_t flags) {
    if(flags & bankSilent)require(std::all_of(samples.begin(),samples.end(),[](float v){return std::bit_cast<std::uint32_t>(v) == 0;}),"Nonzero original silent packet");
}
struct Audio {
    media_io::File descriptor;
    SNDFILE *file=nullptr;
    SF_INFO info{};
    Audio(const std::filesystem::path &path,const OriginalRecordingSpec *s=nullptr) : descriptor(path,s != nullptr) {
        if(s){info.samplerate=int(s->sampleRate);info.channels=int(s->layout.channels);info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;}
        file=sf_open_fd(descriptor.descriptor(),s ? SFM_WRITE : SFM_READ,&info,SF_FALSE);require(file,"Cannot open original RF64 audio");
    }
    ~Audio(){if(file)sf_close(file);}
    void flush(){require(sf_command(file,SFC_UPDATE_HEADER_NOW,nullptr,0) == 0 && sf_error(file) == SF_ERR_NO_ERROR,"Original audio header checkpoint failed");descriptor.flush();}
    void close(){auto *f=file;file=nullptr;require(sf_close(f) == 0,"Original audio close failed");descriptor.flush();descriptor.close();}
};
} // namespace
struct OriginalCaptureWriter::State {
    ResourceLease resources;
    std::filesystem::path job;
    OriginalRecordingPrefix prefix;
    OriginalRecordingOptions options;
    std::unique_ptr<media_io::JobLease> lease;
    std::unique_ptr<Audio> audio;
    std::unique_ptr<media_io::File> stamps;
    media_io::SampleHash samples,metadata;
    std::uint64_t written=0,packets=0,finite=0,committed=0;
    bool failed=false,finalized=false;
    explicit State(ResourceLease r):resources(std::move(r)){}
    void at(OriginalRecordingBoundary b){if(options.boundary)options.boundary(b);}
    void fail() noexcept {failed=true;audio.reset();stamps.reset();lease.reset();}
    void commit() {
        audio->flush();at(OriginalRecordingBoundary::AfterAudioFlush);
        stamps->flush();at(OriginalRecordingBoundary::AfterStampFlush);
        prefix.committedFrames=written;prefix.committedPackets=packets;prefix.finitePrefixFrames=finite;
        prefix.sampleSha256=samples.digest();prefix.stampSha256=metadata.digest();
        at(OriginalRecordingBoundary::BeforeJournal);
        media_io::publishJournal(job/"journal.json",journal(prefix).dump(2)+"\n");committed=written;
        at(OriginalRecordingBoundary::AfterJournal);
    }
};
OriginalCaptureWriter::OriginalCaptureWriter(std::filesystem::path root,OriginalRecordingSpec spec,OriginalRecordingOptions options) {
    validate(spec);
    if(!options.checkpointFrames)options.checkpointFrames=spec.sampleRate;
    require(options.checkpointFrames <= std::uint64_t(spec.sampleRate)*60,"Original checkpoint interval too long");
    PayloadCharge charge("Original recording disk workspace",SIZE_MAX);charge.add(128*1024);charge.add(root.native().size(),8*sizeof(std::filesystem::path::value_type));
    auto reservation=options.resources ? options.resources->reserve(charge.bytes()) : ResourceLease{};
    state_=std::make_unique<State>(std::move(reservation));auto &s=*state_;s.prefix.spec=std::move(spec);s.options=std::move(options);
    media_io::plainDirectory(root);const auto media=root/"media";
    if(std::filesystem::create_directory(media))media_io::flushDirectory(root);
    media_io::plainDirectory(media);
    s.job=media/("original-"+s.prefix.spec.asset.str());require(std::filesystem::create_directory(s.job),"Original recording job exists");
    s.lease=std::make_unique<media_io::JobLease>(s.job,true);media_io::flushDirectory(s.job);media_io::flushDirectory(media);
    s.audio=std::make_unique<Audio>(s.job/"audio.partial.rf64",&s.prefix.spec);
    s.stamps=std::make_unique<media_io::File>(s.job/"packets.bin",true);s.commit();
}
OriginalCaptureWriter::~OriginalCaptureWriter()=default;
const std::filesystem::path &OriginalCaptureWriter::jobDirectory() const noexcept{return state_->job;}
void OriginalCaptureWriter::append(const CaptureBankView &v) {
    auto &s=*state_;
    try {
        require(!s.failed && !s.finalized,"Original writer inactive");
        validatePacket(s.prefix.spec,v.token,s.written,s.packets,s.prefix.last);
        require(v.interleaved.size() == std::size_t(v.token.original.frames)*s.prefix.spec.layout.channels,"Original sample extent mismatch");
        validateSilent(v.interleaved,v.token.original.flags);
        const auto r=record(v.token,s.written);
        s.at(OriginalRecordingBoundary::BeforeAudioWrite);
        require(sf_writef_float(s.audio->file,v.interleaved.data(),v.token.original.frames) == v.token.original.frames &&
                sf_error(s.audio->file) == SF_ERR_NO_ERROR,"Original audio write short/failed");
        s.at(OriginalRecordingBoundary::AfterAudioWrite);
        s.stamps->write({reinterpret_cast<const char *>(r.data()),r.size()});s.at(OriginalRecordingBoundary::AfterStampWrite);
        s.samples.update(v.interleaved);s.metadata.updateBytes(r);
        s.finite=finitePrefix(v.interleaved,s.prefix.spec.layout.channels,s.written,s.finite);
        s.written+=v.token.original.frames;++s.packets;
        if(!s.prefix.first)s.prefix.first=v.token.original;
        s.prefix.last=v.token.original;
        if(s.written-s.committed >= s.options.checkpointFrames)s.commit();
    }catch(...){s.fail();throw;}
}
void OriginalCaptureWriter::checkpoint() {
    auto &s=*state_;try{require(!s.failed && !s.finalized,"Original writer inactive");s.commit();}catch(...){s.fail();throw;}
}
std::string OriginalCaptureWriter::committedJournal() const {return journal(state_->prefix).dump(2)+"\n";}
OriginalRecordingPrefix OriginalCaptureWriter::finalize(const PreparedCaptureFanout &f) {
    auto &s=*state_;
    // Precondition refusal is nonmutating: callers can join/drain correctly then retry.
    const auto &c=f.captureConfig();const auto &p=s.prefix.spec;
    require(!s.failed && !s.finalized && f.settledAfterJoin() && f.producerDone() && f.rawDrained() &&
            f.rawAcceptedFrames() == s.written && s.written && c.identity == p.identity && c.channels == p.layout.channels &&
            c.sampleRate == p.sampleRate && c.maximumPacketFrames == p.maximumPacketFrames &&
            c.initialPacketSequence == p.initialPacketSequence,"Original finalization requires matching joined/drained capture");
    try {
        s.commit();s.audio->close();s.stamps->close();
        s.at(OriginalRecordingBoundary::BeforeMediaPublish);media_io::publishMedia(s.job/"audio.partial.rf64",s.job/"take.wav");
        s.at(OriginalRecordingBoundary::AfterMediaPublish);
        s.prefix.finalized=true;s.prefix.endStatus=f.status();s.prefix.endFault=f.firstFault();
        s.at(OriginalRecordingBoundary::BeforeJournal);media_io::publishJournal(s.job/"journal.json",journal(s.prefix).dump(2)+"\n");
        s.at(OriginalRecordingBoundary::AfterJournal);s.prefix.source=s.job/"take.wav";s.finalized=true;s.lease.reset();return s.prefix;
    }catch(...){s.fail();throw;}
}
OriginalRecordingPrefix detail::inspectOriginalRecordingHeld(const std::filesystem::path &job,const media_io::JobLease &lease,
    const std::optional<OriginalRecordingSpec> &expected,const std::function<void()> &boundary,std::optional<ResourceLedger> resources,
    std::optional<std::string_view> committedJournal) {
    PayloadCharge charge("Original prefix inspection workspace",SIZE_MAX);charge.add(128*1024);
    charge.add(job.native().size(),8*sizeof(std::filesystem::path::value_type));
    auto reservation=resources ? resources->reserve(charge.bytes()) : ResourceLease{};
    auto at=[&]{if(boundary)boundary();};at();media_io::plainDirectory(job);
    require(lease.status() != media_io::LeaseStatus::Busy,"Original writer active");
    require(!committedJournal || committedJournal->size()<=16384,"Original bound journal too large");
    auto p=committedJournal?decode(*committedJournal):decode(media_io::readJournal(job/"journal.json"));
    require(job.filename() == "original-"+p.spec.asset.str() && (!expected || *expected == p.spec),"Original recording context mismatch");
    charge.add(1024*std::size_t(p.spec.layout.channels),sizeof(float));
    if(resources)reservation.resize(charge.bytes()); // Before allocating sample pages/opening audio.
    const auto partial=job/"audio.partial.rf64",final=job/"take.wav";
    const bool a=std::filesystem::exists(std::filesystem::symlink_status(partial)),b=std::filesystem::exists(std::filesystem::symlink_status(final));
    require(!p.finalized || b,"Final original media missing");
    if(a && b){media_io::plainFile(partial);media_io::plainFile(final);require(std::filesystem::equivalent(partial,final),"Ambiguous original media");}
    p.source=a ? partial : final;Audio audio(p.source);media_io::File metadata(job/"packets.bin",false);
    require(audio.info.channels == int(p.spec.layout.channels) && audio.info.samplerate == int(p.spec.sampleRate) &&
            audio.info.frames >= 0 && std::uint64_t(audio.info.frames) >= p.committedFrames &&
            (audio.info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64 &&
            (audio.info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT,"Original audio format/extent mismatch");
    media_io::SampleHash samples,stamps;std::vector<float> buffer(1024*std::size_t(p.spec.layout.channels));
    std::uint64_t frames=0,finite=0;
    for(std::uint64_t n=0;n<p.committedPackets;++n) {
        at();Record r{};metadata.readExact(r);auto t=token(r,frames);validatePacket(p.spec,t,frames,n,p.last);
        require(t.original.frames <= p.committedFrames-frames,"Original packet exceeds common prefix");stamps.updateBytes(r);
        if(!p.first)p.first=t.original;
        std::uint32_t left=t.original.frames;
        while(left) {
            at();const auto count=std::min(left,1024u);
            require(sf_readf_float(audio.file,buffer.data(),count) == count && sf_error(audio.file) == SF_ERR_NO_ERROR,"Original audio prefix unreadable");
            const auto span=std::span<const float>(buffer.data(),std::size_t(count)*p.spec.layout.channels);
            validateSilent(span,t.original.flags);samples.update(span);finite=finitePrefix(span,p.spec.layout.channels,frames,finite);
            frames+=count;left-=count;
        }
        p.last=t.original;
    }
    require(!p.endFault || !p.endFault->rawPacketAccepted || p.endFault->attempted == p.last,"Original fault accepted stamp mismatch");
    require(frames == p.committedFrames && finite == p.finitePrefixFrames && samples.digest() == p.sampleSha256 && stamps.digest() == p.stampSha256,
            "Original common prefix checksum/extent mismatch");at();return p;
}
CaptureBankToken detail::decodeOriginalPacketRecord(std::span<const std::byte,80> bytes,std::uint64_t offset) {
    Record r{};std::copy(bytes.begin(),bytes.end(),r.begin());return token(r,offset);
}
OriginalRecordingPrefix inspectOriginalRecording(const std::filesystem::path &job,const std::optional<OriginalRecordingSpec> &expected,
    const std::function<void()> &boundary,std::optional<ResourceLedger> resources) {
    if(boundary)boundary();
    media_io::plainDirectory(job);media_io::JobLease lease(job,false,true);
    return detail::inspectOriginalRecordingHeld(job,lease,expected,boundary,std::move(resources));
}
} // namespace soundcurrent::daw
