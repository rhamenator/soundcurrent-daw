// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/loop_recording.hpp>
#include "media_io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <set>

namespace soundcurrent::daw {
namespace {
using Json = nlohmann::json;
constexpr std::size_t byteLimit = 128*1024;
void require(bool ok,const char *message) {
    if (!ok) throw ProjectError(ErrorCode::InvalidState,message);
}
void checked(const LoopRecordingContext &c) {
    const auto &i=c.intent;
    require(i.sampleRate >= 8000 && i.sampleRate <= 384000 && i.generation &&
                i.streamBegin >= 0 && i.projectLoop.beginFrame >= 0 &&
                i.projectLoop.endFrame > i.projectLoop.beginFrame &&
                (!i.passes || *i.passes) && !i.tracks.empty() && i.tracks.size() <= 256 &&
                c.sources.size() == i.tracks.size(), "Invalid loop context shape");
    const auto length=i.projectLoop.endFrame-i.projectLoop.beginFrame;
    std::set<std::string> tracks,assets;
    unsigned channels=0;
    for (std::size_t n=0;n<c.sources.size();++n) {
        const auto &b=c.sources[n];
        require(b.track == i.tracks[n] && tracks.insert(b.track.str()).second &&
                    assets.insert(b.asset.str()).second && b.inputLatencyFrames >= 0 &&
                    b.inputLatencyFrames <= Frame(i.sampleRate)*60 &&
                    i.streamBegin < std::numeric_limits<Frame>::max()-b.inputLatencyFrames &&
                    b.captureBegin == i.streamBegin+b.inputLatencyFrames,
                "Invalid loop context binding");
        CaptureConfig cfg;cfg.layout=b.layout;cfg.sampleRate=i.sampleRate;
        cfg.startFrame=b.captureBegin;(void)prepareCaptureConfig(cfg);
        channels+=b.layout.channels;
        require(channels <= 256,"Loop context input channel budget exceeded");
        if (i.passes) require(*i.passes <= std::uint64_t(
            (std::numeric_limits<Frame>::max()-b.captureBegin)/length),
            "Loop context native extent overflows");
    }
}
void fields(const Json &j,std::initializer_list<const char *> names) {
    require(j.is_object() && j.size() == names.size(),"Unknown/missing loop context fields");
    for (const auto *name:names) require(j.contains(name),"Missing loop context field");
}
std::uint64_t integer(const Json &j) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<std::int64_t>() >= 0),
            "Loop context needs a nonnegative integer");
    return j.get<std::uint64_t>();
}
Frame frame(const Json &j) {
    const auto n=integer(j);require(n <= std::uint64_t(std::numeric_limits<Frame>::max()),
                                  "Loop context frame overflow");return Frame(n);
}
std::string text(const Json &j) {
    require(j.is_string() && j.get_ref<const std::string &>().size() <= 128,
            "Invalid loop context string");return j.get<std::string>();
}
std::filesystem::path contextPath(const std::filesystem::path &job) {
    media_io::plainDirectory(job);return job/"loop-context.json";
}
} // namespace
LoopRecordingContext loopRecordingContext(const LoopRecordingPlan &p) {
    LoopRecordingContext c{p.intent,{}};c.sources.reserve(p.lanes.size());
    for (const auto &l:p.lanes) {
        const auto &s=l.spec;
        require(s.projectId == c.intent.project && s.capture.sampleRate == c.intent.sampleRate &&
                    !s.capture.deferredStart && !s.recoveredFrom,"Invalid original loop source");
        c.sources.push_back({s.trackId,s.assetId,s.capture.layout,s.capture.startFrame,s.inputLatencyFrames});
    }
    checked(c);return c;
}
std::string encodeLoopRecordingContext(const LoopRecordingContext &c) {
    checked(c);const auto &i=c.intent;
    Json sources=Json::array();
    for (const auto &b:c.sources) sources.push_back({{"trackId",b.track.str()},
        {"assetId",b.asset.str()},{"layoutKind",std::uint32_t(b.layout.kind)},
        {"channels",b.layout.channels},{"captureBegin",b.captureBegin},
        {"inputLatencyFrames",b.inputLatencyFrames}});
    Json j={{"format","soundcurrent-loop-recording"},{"schemaMajor",1},{"schemaMinor",0},
        {"projectId",i.project.str()},{"sampleRate",i.sampleRate},{"generation",i.generation},
        {"streamBegin",i.streamBegin},{"loop",{{"begin",i.projectLoop.beginFrame},{"end",i.projectLoop.endFrame}}},
        {"passes",i.passes?Json(*i.passes):Json(nullptr)},{"sources",std::move(sources)}};
    auto bytes=j.dump(2)+"\n";require(bytes.size() <= byteLimit,"Loop context size limit exceeded");return bytes;
}
LoopRecordingContext decodeLoopRecordingContext(std::string_view bytes) {
    require(bytes.size() <= byteLimit,"Loop context size limit exceeded");
    try {
        std::vector<std::set<std::string>> keys;
        const auto j=Json::parse(bytes.begin(),bytes.end(),[&](int depth,Json::parse_event_t event,Json &v) {
            require(depth <= 8,"Loop context nesting limit exceeded");
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            else if (event == Json::parse_event_t::key)
                require(!keys.empty() && keys.back().insert(v.get<std::string>()).second,
                        "Duplicate loop context key");
            else if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        fields(j,{"format","schemaMajor","schemaMinor","projectId","sampleRate","generation", "streamBegin","loop","passes","sources"});
        require(text(j.at("format")) == "soundcurrent-loop-recording" &&
                    integer(j.at("schemaMajor")) == 1 && integer(j.at("schemaMinor")) == 0,
                "Unsupported loop context version");
        LoopRecordingContext c{{Id(text(j.at("projectId"))),48000,1,0,{}, {},{}},{}};auto &i=c.intent;
        const auto rate=integer(j.at("sampleRate"));
        require(rate >= 8000 && rate <= 384000,"Invalid loop context sample rate");
        i.sampleRate=std::uint32_t(rate);i.generation=integer(j.at("generation"));
        i.streamBegin=frame(j.at("streamBegin"));fields(j.at("loop"),{"begin","end"});
        i.projectLoop={frame(j.at("loop").at("begin")),frame(j.at("loop").at("end"))};
        if (!j.at("passes").is_null()) i.passes=integer(j.at("passes"));
        const auto &sources=j.at("sources");
        require(sources.is_array() && !sources.empty() && sources.size() <= 256,"Invalid loop context arms");
        for (const auto &b:sources) {
            fields(b,{"trackId","assetId","layoutKind","channels","captureBegin","inputLatencyFrames"});
            const auto kind=integer(b.at("layoutKind")),channels=integer(b.at("channels"));
            require(kind <= std::uint32_t(LayoutKind::Discrete) && channels >= 1 && channels <= 256,
                    "Invalid loop context layout");
            LoopRecordingBinding binding{Id(text(b.at("trackId"))),Id(text(b.at("assetId"))),
                {LayoutKind(kind),std::uint32_t(channels)},frame(b.at("captureBegin")),frame(b.at("inputLatencyFrames"))};
            i.tracks.push_back(binding.track);c.sources.push_back(std::move(binding));
        }
        checked(c);return c;
    } catch (const Json::exception &) {
        throw ProjectError(ErrorCode::InvalidState,"Malformed loop context");
    }
}
void validateLoopRecordingCheckpoint(const LoopRecordingContext &c,const RecordingRecovery &r) {
    checked(c);const auto &i=c.intent;const auto &s=r.spec;
    const auto found=std::find_if(c.sources.begin(),c.sources.end(),[&](const auto &b) {return b.asset == s.assetId;});
    require(found != c.sources.end() && s.projectId == i.project && found->track == s.trackId &&
                s.capture.sampleRate == i.sampleRate && s.capture.layout == found->layout &&
                s.capture.startFrame == found->captureBegin && !s.capture.deferredStart &&
                s.inputLatencyFrames == found->inputLatencyFrames && !s.recoveredFrom &&
                r.committedFrames >= 0 && r.committedFrames <= std::numeric_limits<Frame>::max()-found->captureBegin &&
                (!r.committedFrames || r.timingOrigin),"Loop context differs from original capture journal");
    if (i.passes) require(r.committedFrames <= Frame(*i.passes)*(i.projectLoop.endFrame-i.projectLoop.beginFrame),
                          "Loop checkpoint exceeds requested passes");
    if (r.timingOrigin) require(r.timingOrigin->generation == i.generation &&
        r.timingOrigin->rateNumerator == 1 && r.timingOrigin->rateDenominator == i.sampleRate,
        "Loop checkpoint native origin differs");
}
std::optional<LoopRecordingContext> inspectLoopRecordingContext(const std::filesystem::path &job) {
    const auto path=contextPath(job);
    if (!std::filesystem::exists(std::filesystem::symlink_status(path))) return {};
    media_io::File file(path,false);auto context=decodeLoopRecordingContext(file.readBounded(byteLimit));
    require(std::any_of(context.sources.begin(),context.sources.end(),[&](const auto &b) {
        return job.filename() == "capture-"+b.asset.str();
    }),"Loop context is outside its bound raw job");return context;
}
void persistLoopRecordingContext(const std::filesystem::path &root,const LoopRecordingContext &c) {
    const auto bytes=encodeLoopRecordingContext(c);
    media_io::plainDirectory(root);media_io::plainDirectory(root/"media");
    // Preflight every arm before publishing any copy. Writer-start is complete;
    // the caller owns the activation barrier and has not run an audio producer.
    for (const auto &b:c.sources) {
        const auto job=root/"media"/("capture-"+b.asset.str());
        const auto r=inspectRecording(job);validateLoopRecordingCheckpoint(c,r);
        if (const auto existing=inspectLoopRecordingContext(job))
            require(*existing == c,"Different loop context already owns this raw job");
        else require(!r.finalized && !r.committedFrames && !r.observedFrames && !r.timingOrigin,
                     "Loop intent must be durable before capture begins");
    }
    for (const auto &b:c.sources) {
        const auto job=root/"media"/("capture-"+b.asset.str());
        const auto path=contextPath(job);
        if (const auto existing=inspectLoopRecordingContext(job)) {
            require(*existing == c,"Different loop context already owns this raw job");continue;
        }
        const auto temporary=job/("loop-"+Id::generate().str()+".partial");bool owned=false;
        try {
            media_io::File file(temporary,true);owned=true;file.write(bytes);file.flush();file.close();
            media_io::publishMedia(temporary,path);owned=false;
        } catch (...) {
            if (owned) {std::error_code error;std::filesystem::remove(temporary,error);}
            throw;
        }
    }
}
} // namespace soundcurrent::daw
