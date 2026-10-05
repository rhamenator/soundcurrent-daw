// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
struct RouteIntent {
    std::string backendId; // Stable machine identifier, never a translated label.
    std::string portIdentity;
    bool operator==(const RouteIntent &) const = default;
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
struct Track {
    Id id = Id::generate();
    std::string name;
    ChannelLayout layout;
    RouteIntent input;
    RouteIntent output;
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
struct Session {
    Id id = Id::generate();
    std::string name;
    std::uint32_t sampleRate = 48000;
    Frame playheadFrame = 0;
    Frame exportStartFrame = 0;
    Frame exportEndFrame = 0;
    std::vector<Track> tracks;
    std::vector<Asset> assets;
    bool operator==(const Session &) const = default;
};
bool validUtf8(std::string_view text) noexcept;
void validateRelativeMediaPath(std::string_view path);
void validate(const Session &session);
Session makeOneTrackSession(std::string name, std::string trackName);

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

  private:
    struct Change {
        ParameterAddress address;
        double before;
        double after;
    };
    Session &session_;
    std::optional<Change> active_;
    std::vector<Change> undo_, redo_;
};
} // namespace soundcurrent::daw
