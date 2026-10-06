// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <soundcurrent/session.hpp>
#include <set>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// BCrypt's declarations depend on Win32 types; preserve this include order.
#include <bcrypt.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace soundcurrent::daw {
namespace {
void check(bool good, std::string_view reason, ErrorCode code = ErrorCode::InvalidState) {
    if (!good)
        throw ProjectError(code, std::string(reason));
}
bool lowerHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}
void text(std::string_view value) {
    check(value.size() <= 4096 && value.find('\0') == value.npos && validUtf8(value),
          "Invalid UTF-8 text or text limit exceeded");
}
void layout(const ChannelLayout &l) {
    check(l.channels >= 1 && l.channels <= 256, "Invalid channel count");
    switch (l.kind) {
    case LayoutKind::Mono:
        check(l.channels == 1, "Mono layout needs one channel");
        break;
    case LayoutKind::Stereo:
        check(l.channels == 2, "Stereo layout needs two channels");
        break;
    case LayoutKind::Discrete:
        break;
    default:
        check(false, "Unknown layout kind");
    }
}
const EqBand &findBand(const Session &s, const ParameterAddress &a) {
    for (const auto &t : s.tracks)
        if (t.id == a.trackId && t.eq.id == a.processorId)
            for (const auto &b : t.eq.bands)
                if (b.id == a.bandId)
                    return b;
    throw ProjectError(ErrorCode::InvalidParameter, "Parameter address no longer exists");
}
} // namespace
Id::Id(std::string value) : value_(std::move(value)) {
    check(value_.size() == 36, "Invalid UUID", ErrorCode::InvalidId);
    bool nonzero = false;
    for (std::size_t i = 0; i < value_.size(); ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        check(separator ? value_[i] == '-' : lowerHex(value_[i]),
              "UUID must use canonical lowercase hexadecimal", ErrorCode::InvalidId);
        if (!separator && value_[i] != '0')
            nonzero = true;
    }
    check(nonzero, "Nil UUID is not an object identity", ErrorCode::InvalidId);
}
Id Id::generate() {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    check(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
          "System RNG failed", ErrorCode::Io);
#else
    std::size_t done = 0;
    while (done < bytes.size()) {
        const auto n = getrandom(bytes.data() + done, bytes.size() - done, 0);
        if (n < 0 && errno == EINTR)
            continue;
        check(n > 0, "System RNG failed", ErrorCode::Io);
        done += static_cast<std::size_t>(n);
    }
#endif
    bytes[6] = static_cast<unsigned char>((bytes[6] & 15) | 64);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 63) | 128);
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out += '-';
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 15];
    }
    return Id(std::move(out));
}
bool validUtf8(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto first = static_cast<unsigned char>(s[i++]);
        if (first < 128)
            continue;
        unsigned count = 0, cp = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            cp = first & 31;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            cp = first & 15;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            cp = first & 7;
            minimum = 0x10000;
        } else
            return false;
        if (s.size() - i < count)
            return false;
        for (unsigned n = 0; n < count; ++n) {
            const auto c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
void validateRelativeMediaPath(std::string_view p) {
    text(p);
    check(!p.empty() && p.size() <= 1024 && p[0] != '/' && p.find('\\') == p.npos &&
              p.find(':') == p.npos,
          "Media path must be portable and project relative");
    std::size_t from = 0;
    while (from < p.size()) {
        auto end = p.find('/', from);
        if (end == p.npos)
            end = p.size();
        const auto part = p.substr(from, end - from);
        check(!part.empty() && part != "." && part != ".." && part.back() != '.' &&
                  part.back() != ' ',
              "Invalid media path component");
        for (unsigned char c : part)
            check(c >= 32 && c != 127 && c != '<' && c != '>' && c != '"' && c != '|' && c != '?' &&
                      c != '*',
                  "Nonportable media filename");
        std::string base(part.substr(0, part.find('.')));
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) {
            return c >= 'a' && c <= 'z' ? char(c - 32) : char(c);
        });
        const bool numberedDevice =
            (base.starts_with("COM") || base.starts_with("LPT")) &&
            ((base.size() == 4 && base[3] >= '1' && base[3] <= '9') ||
             (base.size() == 5 && (base.substr(3) == "\xc2\xb9" || base.substr(3) == "\xc2\xb2" ||
                                   base.substr(3) == "\xc2\xb3")));
        const bool device =
            base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" || numberedDevice;
        check(!device, "Reserved Windows device filename");
        from = end + 1;
    }
    check(p.back() != '/', "Media path must name a file");
    check(p.starts_with("media/"), "Media must reside in the media directory");
}
ParameterDescriptor descriptor(BandParameter p) {
    switch (p) {
    case BandParameter::FrequencyHz:
        return {"frequency_hz", 20, 20000, "Hz"};
    case BandParameter::GainDb:
        return {"gain_db", -24, 24, "dB"};
    case BandParameter::Q:
        return {"q", .1, 18, ""};
    }
    throw ProjectError(ErrorCode::InvalidParameter, "Unknown parameter");
}
namespace {
double bandValue(const EqBand &b, BandParameter parameter) {
    switch (parameter) {
    case BandParameter::FrequencyHz:
        return b.frequencyHz;
    case BandParameter::GainDb:
        return b.gainDb;
    case BandParameter::Q:
        return b.q;
    }
    throw ProjectError(ErrorCode::InvalidParameter, "Unknown parameter");
}
} // namespace
double parameterValue(const Session &s, const ParameterAddress &a) {
    return bandValue(findBand(s, a), a.parameter);
}
void setParameterValue(Session &s, const ParameterAddress &a, double value) {
    const auto d = descriptor(a.parameter);
    check(std::isfinite(value) && value >= d.minimum && value <= d.maximum,
          "Parameter outside valid range", ErrorCode::InvalidParameter);
    if (a.parameter == BandParameter::FrequencyHz)
        check(value < double(s.sampleRate) / 2, "EQ frequency exceeds Nyquist",
              ErrorCode::InvalidParameter);
    (void)findBand(s, a);
    for (auto &t : s.tracks)
        if (t.id == a.trackId && t.eq.id == a.processorId)
            for (auto &b : t.eq.bands)
                if (b.id == a.bandId) {
                    switch (a.parameter) {
                    case BandParameter::FrequencyHz:
                        b.frequencyHz = value;
                        break;
                    case BandParameter::GainDb:
                        b.gainDb = value;
                        break;
                    case BandParameter::Q:
                        b.q = value;
                        break;
                    }
                    return;
                }
}
void validate(const Session &s) {
    text(s.name);
    check(s.sampleRate >= 8000 && s.sampleRate <= 384000, "Invalid sample rate");
    check(s.playheadFrame >= 0 && s.exportStartFrame >= 0 && s.exportEndFrame >= s.exportStartFrame,
          "Invalid frame position/range");
    check(s.punch.startFrame >= 0 && s.punch.endFrame >= s.punch.startFrame &&
              (!s.punch.enabled || s.punch.endFrame > s.punch.startFrame),
          "Invalid punch recording locators");
    check(s.tracks.size() <= 256 && s.assets.size() <= 4096, "Session object limit exceeded");
    std::unordered_set<std::string> ids;
    auto unique = [&](const Id &id) {
        check(ids.insert(id.str()).second, "Duplicate object UUID");
    };
    unique(s.id);
    std::unordered_map<std::string, const Asset *> assets;
    for (const auto &a : s.assets) {
        unique(a.id);
        validateRelativeMediaPath(a.relativePath);
        layout(a.layout);
        check(a.sha256.size() == 64 && std::all_of(a.sha256.begin(), a.sha256.end(), lowerHex),
              "Invalid SHA-256");
        check(a.frames > 0 && a.sampleRate >= 8000 && a.sampleRate <= 384000,
              "Invalid asset extent/rate");
        assets.emplace(a.id.str(), &a);
    }
    std::size_t clipCount = 0, routeBytes = 0;
    const auto checkRoute = [&](const RouteIntent &r, std::uint32_t channels) {
        text(r.backendId);
        text(r.portIdentity);
        routeBytes += r.backendId.size() + r.portIdentity.size();
        check(r.ports.empty() ||
                  (r.ports.size() == channels && !r.backendId.empty() && r.portIdentity.empty()),
              "Invalid per-channel route shape or conflicting legacy identity");
        for (const auto &p : r.ports)
            if (p) {
                for (const auto *value : {&p->deviceIdentity, &p->portIdentity, &p->mediaClass}) {
                    text(*value);
                    check(!value->empty(), "Empty route descriptor");
                    routeBytes += value->size();
                }
            }
        check(routeBytes <= 1024 * 1024, "Session route metadata budget exceeded");
    };
    if (s.master) {
        const auto &m = *s.master;
        unique(m.id);
        layout(m.plan.output);
        checkRoute(m.output, m.plan.output.channels);
        for (const auto &p : m.output.ports)
            if (p)
                check(p->input, "Master needs output destinations");
        check(m.plan.tracks.size() <= 256, "Master track limit exceeded");
        std::set<std::string> seen;
        std::size_t routes = 0;
        for (const auto &lane : m.plan.tracks) {
            const auto t = std::find_if(s.tracks.begin(), s.tracks.end(),
                                        [&](const auto &t) { return t.id == lane.track; });
            check(t != s.tracks.end() && seen.insert(lane.track.str()).second,
                  "Master track missing or duplicated");
            check(!lane.channels.empty() && lane.channels.size() <= 65536 - routes,
                  "Master routing limit/empty lane");
            routes += lane.channels.size();
            std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
            for (const auto &c : lane.channels)
                check(c.source < t->layout.channels && c.destination < m.plan.output.channels &&
                          std::isfinite(c.gain) && std::abs(c.gain) <= 64 &&
                          pairs.emplace(c.source, c.destination).second,
                      "Invalid/duplicate master channel route");
        }
    }
    for (const auto &t : s.tracks) {
        unique(t.id);
        unique(t.eq.id);
        text(t.name);
        layout(t.layout);
        check(t.inputLatencyFrames >= 0 && t.inputLatencyFrames <= Frame(s.sampleRate) * 60,
              "Track input latency out of range");
        check(validRecordingMonitor(t.monitoring), "Unknown recording monitoring mode");
        for (const auto *r : {&t.input, &t.output, &t.monitor})
            checkRoute(*r, t.layout.channels);
        for (const auto &p : t.input.ports)
            if (p)
                check(!p->input, "Capture route needs an output endpoint");
        for (const auto *r : {&t.output, &t.monitor})
            for (const auto &p : r->ports)
                if (p)
                    check(p->input, "Playback/monitor route needs an input endpoint");
        check(t.eq.bands.size() <= 64, "EQ band limit exceeded");
        for (const auto &b : t.eq.bands) {
            unique(b.id);
            for (const auto p :
                 {BandParameter::FrequencyHz, BandParameter::GainDb, BandParameter::Q}) {
                const auto d = descriptor(p);
                const auto v = bandValue(b, p); // Already visiting the exact validated object.
                check(std::isfinite(v) && v >= d.minimum && v <= d.maximum, "Invalid EQ parameter");
            }
            check(b.frequencyHz < double(s.sampleRate) / 2, "EQ frequency exceeds Nyquist");
        }
        check(t.clips.size() <= 8192 - clipCount, "Clip limit exceeded");
        clipCount += t.clips.size();
        for (const auto &c : t.clips) {
            unique(c.id);
            auto it = assets.find(c.assetId.str());
            check(it != assets.end(), "Clip refers to unknown asset");
            const auto &a = *it->second;
            check(c.startFrame >= 0 && c.sourceFrame >= 0 && c.lengthFrames > 0,
                  "Invalid clip frame range");
            check(c.sourceFrame <= a.frames && c.lengthFrames <= a.frames - c.sourceFrame,
                  "Clip exceeds source extent");
            check(c.startFrame <= std::numeric_limits<Frame>::max() - c.lengthFrames,
                  "Clip timeline overflow");
            check(t.layout == a.layout, "Clip/track layout mismatch");
        }
    }
}
Session makeOneTrackSession(std::string name, std::string trackName) {
    Session s;
    s.name = std::move(name);
    s.tracks.push_back(makeAudioTrack(std::move(trackName), {}, s.sampleRate));
    validate(s);
    return s;
}
const RouteIntent &routeValue(const Session &s, const RouteAddress &address) {
    if (address.target == RouteTarget::Master) {
        check(s.master && s.master->id == address.trackId, "Master route is missing",
              ErrorCode::InvalidId);
        return s.master->output;
    }
    for (const auto &t : s.tracks)
        if (t.id == address.trackId) {
            switch (address.target) {
            case RouteTarget::Input:
                return t.input;
            case RouteTarget::Output:
                return t.output;
            case RouteTarget::Monitor:
                return t.monitor;
            case RouteTarget::Master:
                break;
            }
            throw ProjectError(ErrorCode::InvalidParameter, "Unknown route target");
        }
    throw ProjectError(ErrorCode::InvalidId, "Route track is missing");
}
RouteIntent patchedRouteValue(const Session &s, const RouteAddress &address,
                              const RouteChannelPatch &patch) {
    auto result = routeValue(s, address);
    const auto track = std::find_if(s.tracks.begin(), s.tracks.end(),
                                    [&](const auto &t) { return t.id == address.trackId; });
    const auto channels = address.target == RouteTarget::Master ? s.master->plan.output.channels
                                                                : track->layout.channels;
    check(patch.channel < channels && !patch.backendId.empty(),
          "Route patch needs a valid channel and backend", ErrorCode::InvalidParameter);
    if (result.backendId != patch.backendId || result.ports.empty())
        result = {patch.backendId, "", std::vector<std::optional<ChannelPortIntent>>(channels)};
    result.ports.at(patch.channel) = patch.port;
    return result;
}
void setRouteValue(Session &s, const RouteAddress &address, const RouteIntent &value) {
    (void)routeValue(s, address);
    auto proposed = s;
    if (address.target == RouteTarget::Master)
        proposed.master->output = value;
    for (auto &t : proposed.tracks)
        if (t.id == address.trackId) {
            switch (address.target) {
            case RouteTarget::Input:
                t.input = value;
                break;
            case RouteTarget::Output:
                t.output = value;
                break;
            case RouteTarget::Monitor:
                t.monitor = value;
                break;
            case RouteTarget::Master:
                break;
            }
        }
    validate(proposed);
    s = std::move(proposed);
}
bool EditHistory::route(const RouteAddress &address, const RouteIntent &value) {
    check(!active_, "Cannot change a route during an active parameter gesture");
    const auto before = routeValue(session_, address);
    if (before == value)
        return false;
    auto proposed = session_;
    setRouteValue(proposed, address, value);
    retain(RouteChange{address, before, value});
    session_ = std::move(proposed);
    return true;
}
RecordingMonitor monitoringValue(const Session &s, const Id &id) {
    for (const auto &t : s.tracks)
        if (t.id == id)
            return t.monitoring;
    throw ProjectError(ErrorCode::InvalidId, "Monitoring track is missing");
}
void setMonitoringValue(Session &s, const Id &id, RecordingMonitor value) {
    check(validRecordingMonitor(value), "Unknown recording monitoring mode",
          ErrorCode::InvalidParameter);
    (void)monitoringValue(s, id);
    for (auto &t : s.tracks)
        if (t.id == id) {
            t.monitoring = value;
            return;
        }
}
bool EditHistory::monitoring(const Id &id, RecordingMonitor value) {
    check(!active_, "Cannot change monitoring during an active parameter gesture");
    const auto before = monitoringValue(session_, id);
    if (before == value)
        return false;
    auto proposed = session_;
    setMonitoringValue(proposed, id, value);
    retain(MonitoringChange{id, before, value});
    session_ = std::move(proposed);
    return true;
}
void EditHistory::begin(const ParameterAddress &address) {
    check(!active_, "A parameter gesture is already active");
    const auto value = parameterValue(session_, address);
    active_ = ParameterChange{address, value, value};
}
void EditHistory::update(double value) {
    check(active_.has_value(), "No active parameter gesture");
    setParameterValue(session_, active_->address, value);
    active_->after = value;
}
void EditHistory::commit() {
    check(active_.has_value(), "No active parameter gesture");
    if (active_->before != active_->after) {
        retain(*active_);
    }
    active_.reset();
}
void EditHistory::cancel() {
    check(active_.has_value(), "No active parameter gesture");
    setParameterValue(session_, active_->address, active_->before);
    active_.reset();
}
bool EditHistory::undo() {
    check(!active_, "Cannot undo an active gesture");
    if (undo_.empty())
        return false;
    const auto change = undo_.back();
    redo_.push_back(change);
    try {
        apply(change, false);
    } catch (...) {
        redo_.pop_back();
        throw;
    }
    undo_.pop_back();
    return true;
}
bool EditHistory::redo() {
    check(!active_, "Cannot redo an active gesture");
    if (redo_.empty())
        return false;
    const auto change = redo_.back();
    undo_.push_back(change);
    try {
        apply(change, true);
    } catch (...) {
        undo_.pop_back();
        throw;
    }
    redo_.pop_back();
    return true;
}
} // namespace soundcurrent::daw
