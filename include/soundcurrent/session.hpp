// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <utility>

namespace soundcurrent::daw {
using Frame = std::int64_t;
enum class ErrorCode {
    InvalidId,
    InvalidState,
    UnsupportedSchema,
    InvalidParameter,
    Io,
    MissingMedia,
    MediaMismatch,
    Canceled
};
class ProjectError : public std::runtime_error {
  public:
    ProjectError(ErrorCode code, const std::string &diagnostic)
        : std::runtime_error(diagnostic), code_(code) {}
    ErrorCode code() const noexcept {
        return code_;
    }

  private:
    ErrorCode code_;
};

class Id {
  public:
    explicit Id(std::string value);
    static Id generate(); // Control thread only: calls the operating-system RNG.
    const std::string &str() const noexcept {
        return value_;
    }
    bool operator==(const Id &) const = default;

  private:
    std::string value_;
};
enum class LayoutKind { Mono, Stereo, Discrete };
struct ChannelLayout {
    LayoutKind kind = LayoutKind::Mono;
    std::uint32_t channels = 1;
    bool operator==(const ChannelLayout &) const = default;
};
struct ChannelPortIntent {
    std::string deviceIdentity, portIdentity, mediaClass;
    bool input = false; // Endpoint receives audio (output/monitor destination).
    bool operator==(const ChannelPortIntent &) const = default;
};
struct RouteIntent {
    std::string backendId; // Stable machine identifier, never a translated label.
    std::string portIdentity;
    // v1.0 opaque identities remain intact; v1.1 uses ordered channel slots.
    std::vector<std::optional<ChannelPortIntent>> ports;
    bool operator==(const RouteIntent &) const = default;
};
struct ChannelMix {
    std::uint32_t source = 0, destination = 0;
    double gain = 1;
    bool operator==(const ChannelMix &) const = default;
};
struct TrackMix {
    Id track;
    std::vector<ChannelMix> channels;
    bool operator==(const TrackMix &) const = default;
};
struct MixPlan {
    ChannelLayout output;
    std::vector<TrackMix> tracks;
    bool operator==(const MixPlan &) const = default;
};
struct MasterBus {
    Id id = Id::generate();
    MixPlan plan;
    RouteIntent output;
    bool operator==(const MasterBus &) const = default;
};
struct EqBand {
    Id id = Id::generate();
    double frequencyHz = 1000;
    double gainDb = 0;
    double q = 1;
    bool operator==(const EqBand &) const = default;
};
struct EqSettings {
    Id id = Id::generate();
    bool enabled = true;
    std::vector<EqBand> bands;
    bool operator==(const EqSettings &) const = default;
};
struct Clip {
    Id id = Id::generate();
    Id assetId = Id::generate();
    Frame startFrame = 0;
    Frame sourceFrame = 0;
    Frame lengthFrames = 0;
    bool operator==(const Clip &) const = default;
};
enum class RecordingMonitor { Off, PostEq };
struct Track {
    Id id = Id::generate();
    std::string name;
    ChannelLayout layout;
    RouteIntent input;
    RouteIntent output;
    RouteIntent monitor;
    RecordingMonitor monitoring = RecordingMonitor::Off;
    Frame inputLatencyFrames = 0; // Declared capture delay, 0..60 seconds at project rate.
    EqSettings eq;
    std::vector<Clip> clips;
    bool operator==(const Track &) const = default;
};
struct Asset {
    Id id = Id::generate();
    std::string relativePath; // Canonical UTF-8 with '/' separators on every OS.
    std::string sha256;
    std::uint32_t sampleRate = 48000;
    ChannelLayout layout;
    Frame frames = 0;
    bool operator==(const Asset &) const = default;
};
struct PunchSettings {
    bool enabled = false;
    Frame startFrame = 0, endFrame = 0; // Desired project frames, before input latency.
    bool operator==(const PunchSettings &) const = default;
};
struct Session {
    Id id = Id::generate();
    std::string name;
    std::uint32_t sampleRate = 48000;
    Frame playheadFrame = 0;
    Frame exportStartFrame = 0;
    Frame exportEndFrame = 0;
    std::vector<Track> tracks;
    std::vector<Asset> assets;
    std::optional<MasterBus> master;
    PunchSettings punch;
    bool operator==(const Session &) const = default;
};
bool validUtf8(std::string_view text) noexcept;
void validateRelativeMediaPath(std::string_view path);
void validate(const Session &session);
Session makeOneTrackSession(std::string name, std::string trackName);
Track makeAudioTrack(std::string name, ChannelLayout layout, std::uint32_t sampleRate);
// Control-thread edits, addressed by stable identity. A batch is all-or-nothing.
struct InsertTrack {
    Track track;
    std::optional<Id> before;
};
struct RemoveTrack {
    Id track;
};
struct RenameTrack {
    Id track;
    std::string name;
};
struct MoveTrack {
    Id track;
    std::optional<Id> before;
};
struct InsertClip {
    Id track;
    Clip clip;
    std::optional<Id> before;
};
struct RemoveClip {
    Id track, clip;
};
struct SetClipRange {
    Id track, clip;
    Frame start, source, length;
};
struct MoveClip {
    Id from, to, clip;
    Frame start;
    std::optional<Id> before;
};
struct SplitClip {
    Id track, clip, rightId;
    Frame position;
};
struct SetMaster {
    std::optional<MasterBus> value;
};
struct SetPunch {
    PunchSettings value;
};
struct SetInputLatency {
    Id track;
    Frame frames;
};
using SessionEdit =
    std::variant<InsertTrack, RemoveTrack, RenameTrack, MoveTrack, InsertClip, RemoveClip,
                 SetClipRange, MoveClip, SplitClip, SetMaster, SetPunch, SetInputLatency>;
void applySessionEdits(Session &, const std::vector<SessionEdit> &);
enum class RouteTarget { Input, Output, Monitor, Master };
struct RouteAddress {
    Id trackId;
    RouteTarget target;
    bool operator==(const RouteAddress &) const = default;
};
struct RouteChannelPatch {
    std::uint32_t channel = 0;
    std::string backendId;
    std::optional<ChannelPortIntent> port;
};
const RouteIntent &routeValue(const Session &, const RouteAddress &);
RouteIntent patchedRouteValue(const Session &, const RouteAddress &, const RouteChannelPatch &);
void setRouteValue(Session &, const RouteAddress &, const RouteIntent &);
RecordingMonitor monitoringValue(const Session &, const Id &trackId);
void setMonitoringValue(Session &, const Id &trackId, RecordingMonitor);

enum class BandParameter { FrequencyHz, GainDb, Q };
struct ParameterAddress {
    Id trackId;
    Id processorId;
    Id bandId;
    BandParameter parameter;
    bool operator==(const ParameterAddress &) const = default;
};
struct ParameterDescriptor {
    std::string_view stableId;
    double minimum;
    double maximum;
    std::string_view unit;
};
ParameterDescriptor descriptor(BandParameter parameter);
double parameterValue(const Session &, const ParameterAddress &);
void setParameterValue(Session &, const ParameterAddress &, double value);

// Semantic control-thread gestures. No audio processing and no RT-safe claim.
class EditHistory {
  public:
    explicit EditHistory(Session &session) : session_(session) {}
    void begin(const ParameterAddress &);
    void update(double value);
    void commit();
    void cancel();
    bool undo();
    bool redo();
    bool route(const RouteAddress &, const RouteIntent &);
    bool monitoring(const Id &trackId, RecordingMonitor);
    bool structural(const std::vector<SessionEdit> &);
    // Verified canonical media admission only; this does no disk verification.
    bool adopt(const Session &);

  private:
    struct ParameterChange {
        ParameterAddress address;
        double before;
        double after;
    };
    struct RouteChange {
        RouteAddress address;
        RouteIntent before, after;
    };
    struct MonitoringChange {
        Id trackId;
        RecordingMonitor before, after;
    };
    template <class T> struct ObjectChange {
        Id id;
        std::optional<T> before, after;
    };
    struct StructureChange {
        std::vector<ObjectChange<Track>> tracks;
        std::vector<ObjectChange<Asset>> assets;
        std::vector<Id> trackOrderBefore, trackOrderAfter, assetOrderBefore, assetOrderAfter;
        std::optional<std::pair<Frame, Frame>> exportEnd;
        std::optional<std::pair<std::optional<MasterBus>, std::optional<MasterBus>>> master;
        std::optional<std::pair<PunchSettings, PunchSettings>> punch;
    };
    using Change = std::variant<ParameterChange, RouteChange, MonitoringChange, StructureChange>;
    static std::size_t weight(const Change &);
    void retain(Change);
    void apply(const Change &, bool forward);
    Session &session_;
    std::optional<ParameterChange> active_;
    std::vector<Change> undo_, redo_;
};
} // namespace soundcurrent::daw
