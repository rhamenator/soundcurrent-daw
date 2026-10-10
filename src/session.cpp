// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <soundcurrent/session.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/stretch.hpp>
#include <set>
#include <filesystem>
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
void validateImportedProjectSource(const ImportedProjectSource &source) {
    PayloadCharge work("Imported source validation indices", 64 * 1024 * 1024);
    work.add(source.media.size(), 256); // Before constructing uniqueness sets.
    const auto digest = [](std::string_view value) {
        check(value.size() == 64 && std::all_of(value.begin(), value.end(), lowerHex),
              "Invalid imported evidence SHA-256");
    };
    check(source.adapterId == "reaper-rpp-properties-v1" ||
              source.adapterId == "reaper-rpp-outline-v1", "Unsupported import evidence adapter",
          ErrorCode::UnsupportedSchema);
    digest(source.sourceSha256);
    check(source.sourceBytes > 0 && source.sourceBytes <= 16 * 1024 * 1024,
          "Imported source exceeds preserved-source envelope");
    const auto prefix = "imports/" + source.id.str() + "/";
    check(source.inspection.relativePath == prefix + "inspection.scinspect",
          "Imported inspection path differs from owned identity");
    digest(source.inspection.sha256);
    check(source.inspection.bytes >= source.sourceBytes + 96 &&
              source.inspection.bytes <= 80 * 1024 * 1024 + 96,
          "Imported inspection extent differs");
    std::set<std::uint64_t> properties;
    std::set<std::string_view> operations;
    for (const auto &media : source.media) {
        check(media.sourceProperty < 1000000 && properties.insert(media.sourceProperty).second &&
                  operations.insert(media.operation.str()).second,
              "Duplicate or out-of-envelope imported media origin");
        check(media.receipt.relativePath == prefix + media.operation.str() + ".json",
              "Imported receipt path differs from owned operation");
        digest(media.receipt.sha256);
        check(media.receipt.bytes > 0 && media.receipt.bytes <= 16384,
              "Imported receipt extent differs");
    }
}
std::filesystem::path utf8Path(std::string_view s) {
    check(validUtf8(s) && s.find('\0') == s.npos, "Invalid UTF-8 path");
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
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
std::size_t sessionPayloadBytes(const Session &s, StateBudget budget) {
    PayloadCharge bytes("Session state/validation", budget.memoryBudgetBytes);
    bytes.add(1, sizeof(Session));
    const auto string = [&](const std::string &v) { bytes.add(v.capacity()); };
    const auto id = [&](const Id &v) {
        string(v.str());
        bytes.add(256); // Validation indices/identity sets, off the audio thread.
    };
    const auto route = [&](const RouteIntent &r) {
        string(r.backendId);
        string(r.portIdentity);
        bytes.add(r.ports.capacity(), sizeof(std::optional<ChannelPortIntent>));
        for (const auto &p : r.ports)
            if (p) {
                string(p->deviceIdentity);
                string(p->portIdentity);
                string(p->mediaClass);
            }
    };
    id(s.id);
    string(s.name);
    bytes.add(s.tracks.capacity(), sizeof(Track));
    bytes.add(s.assets.capacity(), sizeof(Asset));
    for (const auto &a : s.assets) {
        id(a.id);
        string(a.relativePath);
        string(a.sha256);
    }
    bytes.add(s.imports.capacity(), sizeof(ImportedProjectSource));
    const auto evidence = [&](const ProjectEvidenceFile &f) {
        string(f.relativePath); string(f.sha256);
    };
    for (const auto &source : s.imports) {
        id(source.id); string(source.adapterId); string(source.sourceSha256);
        evidence(source.inspection);
        bytes.add(source.media.capacity(), sizeof(ImportedMediaOrigin));
        for (const auto &media : source.media) {
            string(media.assetId.str()); id(media.operation); evidence(media.receipt);
            bytes.add(128); // Per-source property uniqueness index.
        }
    }
    for (const auto &t : s.tracks) {
        id(t.id);
        id(t.eq.id);
        string(t.name);
        route(t.input);
        route(t.output);
        route(t.monitor);
        bytes.add(t.eq.bands.capacity(), sizeof(EqBand));
        for (const auto &b : t.eq.bands)
            id(b.id);
        bytes.add(t.clips.capacity(), sizeof(Clip));
        for (const auto &c : t.clips) {
            validateClipProcessing(c.processing);
            id(c.id);
            string(c.assetId.str());
            if(c.stretch) {
                string(c.stretch->sourceAssetId.str());
                string(c.stretch->sourceSha256);string(c.stretch->renderKey);string(c.stretch->processor);
            }
        }
    }
    if (s.master) {
        id(s.master->id);
        route(s.master->output);
        bytes.add(s.master->plan.tracks.capacity(), sizeof(TrackMix));
        for (const auto &lane : s.master->plan.tracks) {
            string(lane.track.str());
            bytes.add(lane.channels.capacity(), sizeof(ChannelMix) + 128);
        }
    }
    return bytes.bytes();
}
void validate(const Session &s, StateBudget budget) {
    (void)sessionPayloadBytes(s, budget); // Before allocating validation indices.
    text(s.name);
    check(s.sampleRate >= 8000 && s.sampleRate <= 384000, "Invalid sample rate");
    check(s.playheadFrame >= 0 && s.exportStartFrame >= 0 && s.exportEndFrame >= s.exportStartFrame,
          "Invalid frame position/range");
    check(s.punch.startFrame >= 0 && s.punch.endFrame >= s.punch.startFrame &&
              (!s.punch.enabled || s.punch.endFrame > s.punch.startFrame),
          "Invalid punch recording locators");
    std::unordered_set<std::string_view> ids;
    auto unique = [&](const Id &id) {
        check(ids.insert(id.str()).second, "Duplicate object UUID");
    };
    unique(s.id);
    std::unordered_map<std::string_view, const Asset *> assets;
    std::unordered_map<std::string_view, const Track *> tracks;
    for (const auto &t : s.tracks)
        check(tracks.emplace(t.id.str(), &t).second, "Duplicate track UUID");
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
    for (const auto &source : s.imports) {
        unique(source.id);
        validateImportedProjectSource(source);
        for (const auto &media : source.media) {
            unique(media.operation);
            check(assets.contains(media.assetId.str()), "Imported origin refers to missing asset");
        }
    }
    const auto checkRoute = [&](const RouteIntent &r, std::uint32_t channels) {
        text(r.backendId);
        text(r.portIdentity);
        check(r.ports.empty() ||
                  (r.ports.size() == channels && !r.backendId.empty() && r.portIdentity.empty()),
              "Invalid per-channel route shape or conflicting legacy identity");
        for (const auto &p : r.ports)
            if (p) {
                for (const auto *value : {&p->deviceIdentity, &p->portIdentity, &p->mediaClass}) {
                    text(*value);
                    check(!value->empty(), "Empty route descriptor");
                }
            }
    };
    if (s.master) {
        const auto &m = *s.master;
        unique(m.id);
        layout(m.plan.output);
        checkRoute(m.output, m.plan.output.channels);
        for (const auto &p : m.output.ports)
            if (p)
                check(p->input, "Master needs output destinations");
        std::set<std::string_view> seen;
        for (const auto &lane : m.plan.tracks) {
            const auto t = tracks.find(lane.track.str());
            check(t != tracks.end() && seen.insert(lane.track.str()).second,
                  "Master track missing or duplicated");
            check(!lane.channels.empty(), "Empty master routing lane");
            std::set<std::pair<std::uint32_t, std::uint32_t>> pairs;
            for (const auto &c : lane.channels)
                check(c.source < t->second->layout.channels &&
                          c.destination < m.plan.output.channels && std::isfinite(c.gain) &&
                          std::abs(c.gain) <= 64 && pairs.emplace(c.source, c.destination).second,
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
        for (const auto &c : t.clips) {
            unique(c.id);
            auto it = assets.find(c.assetId.str());
            check(it != assets.end(), "Clip refers to unknown asset");
            const auto &a = *it->second;
            check(c.startFrame >= 0 && c.sourceFrame >= 0 && c.lengthFrames > 0,
                  "Invalid clip frame range");
            const auto map=clipSourceMap(c,a.sampleRate,s.sampleRate);
            check(map.at(c.lengthFrames-1).frame<a.frames,
                  "Clip exceeds source extent");
            check(c.startFrame <= std::numeric_limits<Frame>::max() - c.lengthFrames,
                  "Clip timeline overflow");
            check(t.layout == a.layout, "Clip/track layout mismatch");
            if(c.stretch) {
                const auto raw=assets.find(c.stretch->sourceAssetId.str());
                check(raw!=assets.end(),"Stretch refers to missing raw asset");
                validateClipStretch(*c.stretch,*raw->second,a);
            }
        }
    }
}
ValidatedSession::ValidatedSession(const Session &s, StateBudget budget) : session_(s) {
    validate(s, budget);
    tracks_.reserve(s.tracks.size());
    for (const auto &t : s.tracks)
        tracks_.emplace(t.id.str(), &t);
    assets_.reserve(s.assets.size());
    for (const auto &a : s.assets)
        assets_.emplace(a.id.str(), &a);
    imports_.reserve(s.imports.size());
    for (const auto &source : s.imports)
        imports_.emplace(source.id.str(), &source);
}
const ImportedProjectSource &ValidatedSession::importedSource(const Id &id) const {
    const auto found = imports_.find(id.str());
    if (found == imports_.end())
        throw ProjectError(ErrorCode::InvalidId, "Unknown imported source");
    return *found->second;
}
const Track &ValidatedSession::track(const Id &id) const {
    const auto found = tracks_.find(id.str());
    if (found == tracks_.end())
        throw ProjectError(ErrorCode::InvalidId, "Unknown track");
    return *found->second;
}
const Asset &ValidatedSession::asset(const Id &id) const {
    const auto found = assets_.find(id.str());
    if (found == assets_.end())
        throw ProjectError(ErrorCode::InvalidId, "Unknown asset");
    return *found->second;
}
Session makeOneTrackSession(std::string name, std::string trackName, std::uint32_t sampleRate) {
    Session s;
    check(sampleRate >= 8000 && sampleRate <= 384000, "Invalid sample rate");
    s.sampleRate = sampleRate;
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
void setRouteValue(Session &s, const RouteAddress &address, const RouteIntent &value,
                   StateBudget budget) {
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
    validate(proposed, budget);
    s = std::move(proposed);
}
bool EditHistory::route(const RouteAddress &address, const RouteIntent &value) {
    check(!active_, "Cannot change a route during an active parameter gesture");
    const auto before = routeValue(session_, address);
    if (before == value)
        return false;
    auto proposed = session_;
    setRouteValue(proposed, address, value, budget_);
    retain(RouteChange{address, before, value}, proposed);
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
    retain(MonitoringChange{id, before, value}, proposed);
    session_ = std::move(proposed);
    return true;
}
void EditHistory::begin(const ParameterAddress &address) {
    check(!active_, "A parameter gesture is already active");
    const auto value = parameterValue(session_, address);
    const auto peak = checkBegin(address);
    active_ = ParameterChange{address, value, value};
    operationPeakBytes_ = std::max(operationPeakBytes_, peak);
}
void EditHistory::update(double value) {
    check(active_.has_value(), "No active parameter gesture");
    const auto peak = checkOperation(weight(*active_), session_);
    setParameterValue(session_, active_->address, value);
    active_->after = value;
    operationPeakBytes_ = std::max(operationPeakBytes_, peak);
}
void EditHistory::commit() {
    check(active_.has_value(), "No active parameter gesture");
    if (active_->before != active_->after) {
        retain(*active_, session_);
    }
    active_.reset();
}
void EditHistory::cancel() {
    check(active_.has_value(), "No active parameter gesture");
    setParameterValue(session_, active_->address, active_->before);
    active_.reset();
}
bool EditHistory::transfer(bool forward) {
    check(!active_, "Cannot undo or redo an active gesture");
    auto &source = forward ? redo_ : undo_;
    auto &destination = forward ? undo_ : redo_;
    if (source.empty())
        return false;
    const auto &entry = source.back();
    (void)checkOperation(entry.bytes, session_, false);
    auto next = proposed(entry.change, forward);
    const auto peak = checkOperation(entry.bytes, next, false);
    destination.push_back(entry); // Copies before canonical mutation; allocation may fail.
    session_ = std::move(next);
    source.pop_back();
    operationPeakBytes_ = std::max(operationPeakBytes_, peak);
    return true;
}
std::size_t EditHistory::checkUpdate() const {
    check(active_.has_value(), "No active parameter gesture");
    return checkOperation(weight(*active_), session_);
}
std::size_t EditHistory::checkCommit() const {
    return active_ && active_->before != active_->after ? checkCandidate(*active_, session_) : 0;
}
std::optional<Session> EditHistory::previewTransfer(bool forward, std::size_t *declaredPeak) const {
    if (declaredPeak)
        *declaredPeak = 0;
    if (active_ && active_->before != active_->after) {
        if (forward)
            return {}; // Committing this edit clears Redo.
        (void)checkOperation(weight(*active_), session_);
        auto next = proposed(*active_, false);
        const auto peak = checkOperation(weight(*active_), next);
        if (declaredPeak)
            *declaredPeak = peak;
        return next;
    }
    const auto &source = forward ? redo_ : undo_;
    if (source.empty())
        return {};
    (void)checkOperation(source.back().bytes, session_, false);
    auto next = proposed(source.back().change, forward);
    const auto peak = checkOperation(source.back().bytes, next, false);
    if (declaredPeak)
        *declaredPeak = peak;
    return next;
}
bool EditHistory::undo() {
    return transfer(false);
}
bool EditHistory::redo() {
    return transfer(true);
}
} // namespace soundcurrent::daw
