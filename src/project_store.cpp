// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <soundcurrent/project_store.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <unordered_set>
#ifdef SC_STORE_IMPORT_STATE
#include <soundcurrent/project_import_state.hpp>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// BCrypt's declarations depend on Win32 types; preserve this include order.
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace soundcurrent::daw {
namespace {
using Json = nlohmann::json;
[[noreturn]] void fail(std::string_view s, ErrorCode code = ErrorCode::InvalidState) {
    throw ProjectError(code, std::string(s));
}
void require(bool ok, std::string_view s, ErrorCode code = ErrorCode::InvalidState) {
    if (!ok)
        fail(s, code);
}
void keys(const Json &j, std::initializer_list<std::string_view> names) {
    require(j.is_object() && j.size() == names.size(), "Unexpected project fields");
    for (auto n : names)
        require(j.contains(std::string(n)), "Missing project field");
}
std::string string(const Json &j) {
    require(j.is_string(), "Expected string");
    return j.get<std::string>();
}
Frame integer(const Json &j) {
    require(j.is_number_integer(), "Expected integer");
    if (j.is_number_unsigned())
        require(j.get<std::uint64_t>() <= std::uint64_t(std::numeric_limits<Frame>::max()),
                "Integer overflow");
    return j.get<Frame>();
}
std::uint32_t u32(const Json &j) {
    const auto n = integer(j);
    require(n >= 0 && n <= std::numeric_limits<std::uint32_t>::max(), "Integer out of range");
    return static_cast<std::uint32_t>(n);
}
std::uint64_t u64(const Json &j) {
    require(j.is_number_integer(),"Expected unsigned integer");
    if(j.is_number_unsigned()) return j.get<std::uint64_t>();
    require(j.get<Frame>()>=0,"Negative unsigned integer");
    return std::uint64_t(j.get<Frame>());
}
bool boolean(const Json &j) {
    require(j.is_boolean(), "Expected boolean");
    return j.get<bool>();
}
double number(const Json &j) {
    require(j.is_number(), "Expected numeric parameter");
    return j.get<double>();
}
void array(const Json &j, std::size_t limit) {
    require(j.is_array() && j.size() <= limit, "Array limit exceeded");
}
Json layout(const ChannelLayout &l) {
    return {{"kind", l.kind == LayoutKind::Mono     ? "mono"
                     : l.kind == LayoutKind::Stereo ? "stereo"
                                                    : "discrete"},
            {"channels", l.channels}};
}
ChannelLayout readLayout(const Json &j) {
    keys(j, {"kind", "channels"});
    auto k = string(j.at("kind"));
    require(k == "mono" || k == "stereo" || k == "discrete", "Unknown layout");
    return {k == "mono"     ? LayoutKind::Mono
            : k == "stereo" ? LayoutKind::Stereo
                            : LayoutKind::Discrete,
            u32(j.at("channels"))};
}
Json route(const RouteIntent &r) {
    auto ports = Json::array();
    for (const auto &p : r.ports) {
        if (!p)
            ports.push_back(nullptr);
        else
            ports.push_back({{"deviceIdentity", p->deviceIdentity},
                             {"portIdentity", p->portIdentity},
                             {"mediaClass", p->mediaClass},
                             {"input", p->input}});
    }
    return {{"backendId", r.backendId}, {"portIdentity", r.portIdentity}, {"ports", ports}};
}
RouteIntent readRoute(const Json &j, bool legacy) {
    if (legacy)
        keys(j, {"backendId", "portIdentity"});
    else
        keys(j, {"backendId", "portIdentity", "ports"});
    RouteIntent r{string(j.at("backendId")), string(j.at("portIdentity")), {}};
    if (!legacy) {
        array(j.at("ports"), 256);
        for (const auto &p : j.at("ports")) {
            if (p.is_null())
                r.ports.emplace_back();
            else {
                keys(p, {"deviceIdentity", "portIdentity", "mediaClass", "input"});
                r.ports.push_back(
                    ChannelPortIntent{string(p.at("deviceIdentity")), string(p.at("portIdentity")),
                                      string(p.at("mediaClass")), boolean(p.at("input"))});
            }
        }
    }
    return r;
}
bool plainFile(const std::filesystem::path &p) {
    const auto s = std::filesystem::symlink_status(p);
    if (!std::filesystem::is_regular_file(s))
        return false;
#ifdef _WIN32
    auto attributes = GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
#endif
    return true;
}
void noLink(const std::filesystem::path &p) {
    require(!std::filesystem::is_symlink(std::filesystem::symlink_status(p)),
            "Linked project path refused", ErrorCode::Io);
#ifdef _WIN32
    auto a = GetFileAttributesW(p.c_str());
    require(a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_REPARSE_POINT),
            "Reparse point refused", ErrorCode::Io);
#endif
}
std::string readFile(const std::filesystem::path &p, ProjectBudget budget) {
    require(plainFile(p), "Project file missing or not a regular file", ErrorCode::Io);
    PayloadCharge bytes("Encoded project", budget.encodedBytes);
    bytes.add(std::filesystem::file_size(p));
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "Cannot read project", ErrorCode::Io);
    std::string out;
    std::array<char, 8192> buffer{};
    while (f) {
        f.read(buffer.data(), buffer.size());
        out.append(buffer.data(), static_cast<std::size_t>(f.gcount()));
        if (out.size() > budget.encodedBytes)
            throw ResourceLimitError("Encoded project", out.size(), budget.encodedBytes);
    }
    require(f.eof(), "Project read failed", ErrorCode::Io);
    return out;
}
class WriterLock {
  public:
    explicit WriterLock(const std::filesystem::path &root) {
        const auto p = root / ".save.lock";
        noLink(p);
#ifdef _WIN32
        handle_ = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        require(handle_ != INVALID_HANDLE_VALUE, "Project is locked or unavailable", ErrorCode::Io);
#else
        fd_ = open(p.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        require(fd_ >= 0, "Cannot open project lock", ErrorCode::Io);
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            close(fd_);
            fd_ = -1;
            fail("Project is locked", ErrorCode::Io);
        }
#endif
    }
    ~WriterLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
#else
        if (fd_ >= 0)
            close(fd_);
#endif
    }
    WriterLock(const WriterLock &) = delete;
    WriterLock &operator=(const WriterLock &) = delete;

  private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};
Durability publish(const std::filesystem::path &destination, std::string_view bytes,
                   const SaveOptions &options = {}) {
    noLink(destination);
    if (std::filesystem::exists(destination))
        require(plainFile(destination), "Cannot replace nonregular project file", ErrorCode::Io);
    const auto tmp = destination.parent_path() / utf8Path("." + destination.filename().string() +
                                                          "." + Id::generate().str() + ".partial");
    struct Cleanup {
        std::filesystem::path path;
        bool owned = false;
        ~Cleanup() {
            if (owned) {
                std::error_code e;
                std::filesystem::remove(path, e);
            }
        }
    } cleanup{tmp};
#ifdef _WIN32
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    require(h != INVALID_HANDLE_VALUE, "Cannot create temporary project", ErrorCode::Io);
    cleanup.owned = true;
    bool ok = true;
    std::size_t done = 0;
    while (done < bytes.size()) {
        DWORD n = 0;
        ok = WriteFile(h, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &n,
                       nullptr) &&
             n > 0;
        if (!ok)
            break;
        done += n;
    }
    ok = ok && FlushFileBuffers(h);
    const bool closed = CloseHandle(h) != 0;
    require(ok && closed, "Cannot flush temporary project", ErrorCode::Io);
    if (options.beforePublish)
        options.beforePublish();
    require(MoveFileExW(tmp.c_str(), destination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
            "Cannot publish project", ErrorCode::Io);
    return Durability::FileFlushed;
#else
    const int fd = open(tmp.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd >= 0, "Cannot create temporary project", ErrorCode::Io);
    cleanup.owned = true;
    bool ok = true;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const auto n = write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        done += static_cast<std::size_t>(n);
    }
    ok = ok && fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    require(ok && closed, "Cannot flush temporary project", ErrorCode::Io);
    if (options.beforePublish)
        options.beforePublish();
    require(rename(tmp.c_str(), destination.c_str()) == 0, "Cannot publish project", ErrorCode::Io);
    const int directory =
        open(destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0)
        return Durability::FileFlushed;
    const bool synced = fsync(directory) == 0;
    close(directory);
    return synced ? Durability::FileAndDirectoryFlushed : Durability::FileFlushed;
#endif
}
} // namespace
std::string encodeProject(const Session &s, ProjectBudget budget) {
    validate(s, budget.state);
    PayloadCharge staging("Project encoding staging", budget.parserBytes);
    staging.add(sessionPayloadBytes(s, budget.state), 8);
    Json tracks = Json::array(), assets = Json::array();
    const auto fade = [](const ClipFade &f) {
        const char *curve = f.curve == ClipFadeCurve::Linear ? "linear" :
            f.curve == ClipFadeCurve::EqualPower ? "equal-power" : "smoothstep";
        return Json{{"startFrame",f.startFrame},{"endFrame",f.endFrame},
                    {"curve",curve},{"shape",f.shape}};
    };
    for (const auto &t : s.tracks) {
        Json bands = Json::array(), clips = Json::array();
        for (const auto &b : t.eq.bands)
            bands.push_back({{"id", b.id.str()},
                             {"frequencyHz", b.frequencyHz},
                             {"gainDb", b.gainDb},
                             {"q", b.q}});
        for (const auto &c : t.clips)
            clips.push_back({{"id", c.id.str()},
                             {"assetId", c.assetId.str()},
                             {"startFrame", c.startFrame},
                             {"sourceFrame", c.sourceFrame},
                             {"lengthFrames", c.lengthFrames},
                             {"sourceTiming",{{"fraction",c.sourceTiming.fraction},
                                 {"denominator",c.sourceTiming.denominator},
                                 {"algorithm",positionedResamplingAlgorithmId}}},
                             {"processing", {{"gainDb",c.processing.gainDb},
                                 {"muted",c.processing.muted},
                                 {"polarityInverted",c.processing.polarityInverted},
                                 {"fadeIn",fade(c.processing.fadeIn)},
                                 {"fadeOut",fade(c.processing.fadeOut)}}}});
        Json processor = {{"id", t.eq.id.str()},
                          {"type", "sc.eq"},
                          {"version", 1},
                          {"enabled", t.eq.enabled},
                          {"bands", bands}};
        tracks.push_back(
            {{"id", t.id.str()},
             {"name", t.name},
             {"layout", layout(t.layout)},
             {"inputIntent", route(t.input)},
             {"outputIntent", route(t.output)},
             {"monitorIntent", route(t.monitor)},
             {"monitoringMode", t.monitoring == RecordingMonitor::Off      ? "off"
                                : t.monitoring == RecordingMonitor::PostEq ? "post-eq"
                                                                           : "auto-recording"},
             {"inputLatencyFrames", t.inputLatencyFrames},
             {"processors", Json::array({processor})},
             {"clips", clips}});
    }
    for (const auto &a : s.assets)
        assets.push_back({{"id", a.id.str()},
                          {"path", a.relativePath},
                          {"sha256", a.sha256},
                          {"sampleRate", a.sampleRate},
                          {"layout", layout(a.layout)},
                          {"frames", a.frames}});
    Json root = {
        {"format", "soundcurrent-daw"},
        {"schemaMajor", 1},
        {"schemaMinor", 10},
        {"projectId", s.id.str()},
        {"name", s.name},
        {"sampleRate", s.sampleRate},
        {"playheadFrame", s.playheadFrame},
        {"exportRange", {{"startFrame", s.exportStartFrame}, {"endFrame", s.exportEndFrame}}},
        {"punchRecording",
         {{"enabled", s.punch.enabled},
          {"startFrame", s.punch.startFrame},
          {"endFrame", s.punch.endFrame}}},
        {"tracks", tracks},
        {"assets", assets}};
    root["imports"] = Json::array();
    const auto file = [](const ProjectEvidenceFile &f) {
        return Json{{"path", f.relativePath}, {"sha256", f.sha256}, {"bytes", f.bytes}};
    };
    for (const auto &source : s.imports) {
        Json media = Json::array();
        for (const auto &origin : source.media)
            media.push_back({{"assetId", origin.assetId.str()}, {"operation", origin.operation.str()},
                             {"sourceProperty", origin.sourceProperty}, {"receipt", file(origin.receipt)}});
        root["imports"].push_back({{"id", source.id.str()}, {"adapterId", source.adapterId},
                                  {"sourceSha256", source.sourceSha256}, {"sourceBytes", source.sourceBytes},
                                  {"inspection", file(source.inspection)}, {"media", media}});
    }
    root["master"] = nullptr;
    if (s.master) {
        Json lanes = Json::array();
        for (const auto &t : s.master->plan.tracks) {
            Json maps = Json::array();
            for (const auto &c : t.channels)
                maps.push_back(
                    {{"source", c.source}, {"destination", c.destination}, {"gain", c.gain}});
            lanes.push_back({{"trackId", t.track.str()}, {"channels", maps}});
        }
        root["master"] = {{"id", s.master->id.str()},
                          {"layout", layout(s.master->plan.output)},
                          {"tracks", lanes},
                          {"outputIntent", route(s.master->output)}};
    }
    auto out = root.dump(2) + "\n";
    PayloadCharge encoded("Encoded project", budget.encodedBytes);
    encoded.add(out.size());
    return out;
}
namespace {
// A non-building pass certifies bounded node/string/duplicate-key work before
// constructing the JSON DOM. This is a payload charge, not an allocator/RSS cap.
struct ProjectPreflight : nlohmann::json_sax<Json> {
    PayloadCharge charge;
    struct Level {
        bool object;
        std::unordered_set<std::string> keys;
    };
    std::vector<Level> levels;
    explicit ProjectPreflight(std::size_t budget) : charge("Project parser staging", budget) {}
    bool scalar() {
        charge.add(128);
        return true;
    }
    bool null() override {
        return scalar();
    }
    bool boolean(bool) override {
        return scalar();
    }
    bool number_integer(number_integer_t) override {
        return scalar();
    }
    bool number_unsigned(number_unsigned_t) override {
        return scalar();
    }
    bool number_float(number_float_t, const string_t &) override {
        return scalar();
    }
    bool string(string_t &v) override {
        charge.add(v.size());
        return scalar();
    }
    bool binary(binary_t &) override {
        fail("Binary project JSON refused");
    }
    bool start(bool object) {
        require(levels.size() < 32, "Project nesting limit exceeded");
        charge.add(512);
        levels.push_back({object, {}});
        return true;
    }
    bool start_object(std::size_t) override {
        return start(true);
    }
    bool start_array(std::size_t) override {
        return start(false);
    }
    bool key(string_t &v) override {
        charge.add(128);
        charge.add(v.size());
        require(!levels.empty() && levels.back().object && levels.back().keys.insert(v).second,
                "Duplicate JSON key");
        return true;
    }
    bool end_object() override {
        levels.pop_back();
        return true;
    }
    bool end_array() override {
        levels.pop_back();
        return true;
    }
    bool parse_error(std::size_t, const std::string &,
                     const nlohmann::detail::exception &) override {
        fail("Malformed project JSON");
    }
};
void canonicalPreflight(const Json &value, PayloadCharge &charge) {
    if (value.is_object()) {
        charge.add(512);
        for (const auto &child : value.items())
            canonicalPreflight(child.value(), charge);
    } else if (value.is_array()) {
        charge.add(128);
        for (const auto &child : value)
            canonicalPreflight(child, charge);
    } else {
        charge.add(32);
        if (value.is_string())
            charge.add(value.get_ref<const std::string &>().size());
    }
}
} // namespace
Session decodeProject(std::string_view bytes, ProjectBudget budget) {
    PayloadCharge encoded("Encoded project", budget.encodedBytes);
    encoded.add(bytes.size());
    try {
        ProjectPreflight preflight(budget.parserBytes);
        preflight.charge.add(
            bytes.size()); // Token strings are bounded before SAX materializes them.
        require(Json::sax_parse(bytes.begin(), bytes.end(), &preflight), "Malformed project JSON");
        const auto j = Json::parse(bytes.begin(), bytes.end());
        require(j.is_object() && j.contains("schemaMajor") && j.contains("schemaMinor"),
                "Missing project schema");
        const auto minor = integer(j.at("schemaMinor"));
        require(integer(j.at("schemaMajor")) == 1 && (minor >= 0 && minor <= 10),
                "Unsupported project schema", ErrorCode::UnsupportedSchema);
        if (minor < 3)
            keys(j, {"format", "schemaMajor", "schemaMinor", "projectId", "name", "sampleRate",
                     "playheadFrame", "exportRange", "tracks", "assets"});
        else if (minor == 3)
            keys(j, {"format", "schemaMajor", "schemaMinor", "projectId", "name", "sampleRate",
                     "playheadFrame", "exportRange", "tracks", "assets", "master"});
        else if (minor < 8)
            keys(j,
                 {"format", "schemaMajor", "schemaMinor", "projectId", "name", "sampleRate",
                  "playheadFrame", "exportRange", "tracks", "assets", "master", "punchRecording"});
        else
            keys(j,
                 {"format", "schemaMajor", "schemaMinor", "projectId", "name", "sampleRate",
                  "playheadFrame", "exportRange", "tracks", "assets", "master", "punchRecording", "imports"});
        require(string(j.at("format")) == "soundcurrent-daw", "Unrecognized project format");
        PayloadCharge canonical("Project canonical staging", budget.state.memoryBudgetBytes);
        canonicalPreflight(j, canonical); // Before constructing canonical vectors/strings.
        Session s;
        s.id = Id(string(j.at("projectId")));
        s.name = string(j.at("name"));
        s.sampleRate = u32(j.at("sampleRate"));
        s.playheadFrame = integer(j.at("playheadFrame"));
        const auto &range = j.at("exportRange");
        keys(range, {"startFrame", "endFrame"});
        s.exportStartFrame = integer(range.at("startFrame"));
        s.exportEndFrame = integer(range.at("endFrame"));
        if (minor >= 4) {
            const auto &punch = j.at("punchRecording");
            keys(punch, {"enabled", "startFrame", "endFrame"});
            s.punch = {boolean(punch.at("enabled")), integer(punch.at("startFrame")),
                       integer(punch.at("endFrame"))};
        }
        array(j.at("assets"), budget.state.memoryBudgetBytes / sizeof(Asset));
        s.assets.reserve(j.at("assets").size());
        for (const auto &a : j.at("assets")) {
            keys(a, {"id", "path", "sha256", "sampleRate", "layout", "frames"});
            Asset asset;
            asset.id = Id(string(a.at("id")));
            asset.relativePath = string(a.at("path"));
            asset.sha256 = string(a.at("sha256"));
            asset.sampleRate = u32(a.at("sampleRate"));
            asset.layout = readLayout(a.at("layout"));
            asset.frames = integer(a.at("frames"));
            s.assets.push_back(std::move(asset));
        }
        array(j.at("tracks"), budget.state.memoryBudgetBytes / sizeof(Track));
        s.tracks.reserve(j.at("tracks").size());
        for (const auto &t : j.at("tracks")) {
            if (minor == 0)
                keys(t, {"id", "name", "layout", "inputIntent", "outputIntent", "processors",
                         "clips"});
            else if (minor == 1)
                keys(t, {"id", "name", "layout", "inputIntent", "outputIntent", "monitorIntent",
                         "processors", "clips"});
            else if (minor < 5)
                keys(t, {"id", "name", "layout", "inputIntent", "outputIntent", "monitorIntent",
                         "monitoringMode", "processors", "clips"});
            else
                keys(t, {"id", "name", "layout", "inputIntent", "outputIntent", "monitorIntent",
                         "monitoringMode", "inputLatencyFrames", "processors", "clips"});
            Track track;
            track.id = Id(string(t.at("id")));
            track.name = string(t.at("name"));
            track.layout = readLayout(t.at("layout"));
            if (minor >= 5)
                track.inputLatencyFrames = integer(t.at("inputLatencyFrames"));
            track.input = readRoute(t.at("inputIntent"), minor == 0);
            track.output = readRoute(t.at("outputIntent"), minor == 0);
            if (minor >= 1)
                track.monitor = readRoute(t.at("monitorIntent"), false);
            if (minor >= 2) {
                const auto mode = string(t.at("monitoringMode"));
                require(mode == "off" || mode == "post-eq" ||
                            (minor >= 6 && mode == "auto-recording"),
                        "Unknown recording monitoring mode");
                track.monitoring = mode == "off"       ? RecordingMonitor::Off
                                   : mode == "post-eq" ? RecordingMonitor::PostEq
                                                       : RecordingMonitor::AutoRecording;
            }
            const auto &ps = t.at("processors");
            array(ps, 1);
            require(ps.size() == 1, "Exactly one EQ expected in schema v1");
            const auto &p = ps[0];
            keys(p, {"id", "type", "version", "enabled", "bands"});
            require(string(p.at("type")) == "sc.eq" && integer(p.at("version")) == 1,
                    "Unsupported processor", ErrorCode::UnsupportedSchema);
            track.eq.id = Id(string(p.at("id")));
            track.eq.enabled = boolean(p.at("enabled"));
            array(p.at("bands"), 64);
            track.eq.bands.reserve(p.at("bands").size());
            for (const auto &b : p.at("bands")) {
                keys(b, {"id", "frequencyHz", "gainDb", "q"});
                EqBand band;
                band.id = Id(string(b.at("id")));
                band.frequencyHz = number(b.at("frequencyHz"));
                band.gainDb = number(b.at("gainDb"));
                band.q = number(b.at("q"));
                track.eq.bands.push_back(std::move(band));
            }
            array(t.at("clips"), budget.state.memoryBudgetBytes / sizeof(Clip));
            track.clips.reserve(t.at("clips").size());
            for (const auto &c : t.at("clips")) {
                if (minor < 9)
                    keys(c, {"id", "assetId", "startFrame", "sourceFrame", "lengthFrames"});
                else if(minor==9)
                    keys(c, {"id", "assetId", "startFrame", "sourceFrame", "lengthFrames", "processing"});
                else
                    keys(c, {"id", "assetId", "startFrame", "sourceFrame", "lengthFrames", "processing", "sourceTiming"});
                Clip clip;
                clip.id = Id(string(c.at("id")));
                clip.assetId = Id(string(c.at("assetId")));
                clip.startFrame = integer(c.at("startFrame"));
                clip.sourceFrame = integer(c.at("sourceFrame"));
                clip.lengthFrames = integer(c.at("lengthFrames"));
                if(minor>=10) {
                    const auto &timing=c.at("sourceTiming");keys(timing,{"fraction","denominator","algorithm"});
                    require(string(timing.at("algorithm"))==positionedResamplingAlgorithmId,
                            "Unsupported clip source algorithm",ErrorCode::UnsupportedSchema);
                    clip.sourceTiming={u64(timing.at("fraction")),u64(timing.at("denominator"))};
                } else {
                    const auto asset=std::find_if(s.assets.begin(),s.assets.end(),
                        [&](const auto &a){return a.id==clip.assetId;});
                    require(asset==s.assets.end() || asset->sampleRate==s.sampleRate,
                            "Legacy mixed-rate timing requires explicit conversion",ErrorCode::UnsupportedSchema);
                }
                if (minor >= 9) {
                    const auto &p = c.at("processing");
                    keys(p,{"gainDb","muted","polarityInverted","fadeIn","fadeOut"});
                    clip.processing.gainDb = number(p.at("gainDb"));
                    clip.processing.muted = boolean(p.at("muted"));
                    clip.processing.polarityInverted = boolean(p.at("polarityInverted"));
                    const auto readFade = [&](const Json &f) {
                        keys(f,{"startFrame","endFrame","curve","shape"});
                        const auto curve = string(f.at("curve"));
                        require(curve=="linear" || curve=="equal-power" || curve=="smoothstep",
                                "Unknown clip fade curve",ErrorCode::UnsupportedSchema);
                        return ClipFade{integer(f.at("startFrame")),integer(f.at("endFrame")),
                            curve=="linear" ? ClipFadeCurve::Linear :
                            curve=="equal-power" ? ClipFadeCurve::EqualPower : ClipFadeCurve::Smoothstep,
                            number(f.at("shape"))};
                    };
                    clip.processing.fadeIn = readFade(p.at("fadeIn"));
                    clip.processing.fadeOut = readFade(p.at("fadeOut"));
                }
                track.clips.push_back(std::move(clip));
            }
            s.tracks.push_back(std::move(track));
        }
        if (minor >= 3 && !j.at("master").is_null()) {
            const auto &m = j.at("master");
            keys(m, {"id", "layout", "tracks", "outputIntent"});
            MasterBus master;
            master.id = Id(string(m.at("id")));
            master.plan.output = readLayout(m.at("layout"));
            master.output = readRoute(m.at("outputIntent"), false);
            array(m.at("tracks"), budget.state.memoryBudgetBytes / sizeof(TrackMix));
            master.plan.tracks.reserve(m.at("tracks").size());
            for (const auto &t : m.at("tracks")) {
                keys(t, {"trackId", "channels"});
                array(t.at("channels"), budget.state.memoryBudgetBytes / sizeof(ChannelMix));
                TrackMix lane{Id(string(t.at("trackId"))), {}};
                lane.channels.reserve(t.at("channels").size());
                for (const auto &c : t.at("channels")) {
                    keys(c, {"source", "destination", "gain"});
                    lane.channels.push_back(
                        {u32(c.at("source")), u32(c.at("destination")), number(c.at("gain"))});
                }
                master.plan.tracks.push_back(std::move(lane));
            }
            s.master = std::move(master);
        }
        if (minor >= 8) {
            const auto file = [&](const Json &f) {
                keys(f, {"path", "sha256", "bytes"});
                const auto bytes = integer(f.at("bytes"));
                require(bytes > 0, "Invalid project evidence extent");
                return ProjectEvidenceFile{string(f.at("path")), string(f.at("sha256")),
                                           static_cast<std::uint64_t>(bytes)};
            };
            array(j.at("imports"), budget.state.memoryBudgetBytes / sizeof(ImportedProjectSource));
            s.imports.reserve(j.at("imports").size());
            for (const auto &i : j.at("imports")) {
                keys(i, {"id", "adapterId", "sourceSha256", "sourceBytes", "inspection", "media"});
                ImportedProjectSource source;
                source.id = Id(string(i.at("id")));
                source.adapterId = string(i.at("adapterId"));
                source.sourceSha256 = string(i.at("sourceSha256"));
                const auto bytes = integer(i.at("sourceBytes"));
                require(bytes > 0, "Invalid imported source extent");
                source.sourceBytes = static_cast<std::uint64_t>(bytes);
                source.inspection = file(i.at("inspection"));
                array(i.at("media"), budget.state.memoryBudgetBytes / sizeof(ImportedMediaOrigin));
                source.media.reserve(i.at("media").size());
                for (const auto &m : i.at("media")) {
                    keys(m, {"assetId", "operation", "sourceProperty", "receipt"});
                    ImportedMediaOrigin origin;
                    origin.assetId = Id(string(m.at("assetId")));
                    origin.operation = Id(string(m.at("operation")));
                    const auto property = integer(m.at("sourceProperty"));
                    require(property >= 0, "Invalid imported source property");
                    origin.sourceProperty = static_cast<std::uint64_t>(property);
                    origin.receipt = file(m.at("receipt"));
                    source.media.push_back(std::move(origin));
                }
                s.imports.push_back(std::move(source));
            }
        }
        validate(s, budget.state);
        return s;
    } catch (const Json::exception &) {
        fail("Malformed project JSON");
    }
}
std::string hashMediaFile(const std::filesystem::path &p, const std::function<void()> &beforeRead) {
    require(plainFile(p), "Media missing or not a regular file", ErrorCode::MissingMedia);
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "Cannot read media", ErrorCode::Io);
    std::array<unsigned char, 32> digest{};
    std::array<char, 65536> buffer{};
#ifdef _WIN32
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<unsigned char> object;
        ~Hash() {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } h;
    require(BCryptOpenAlgorithmProvider(&h.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "SHA-256 unavailable", ErrorCode::Io);
    DWORD size = 0, returned = 0;
    require(BCryptGetProperty(h.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                              sizeof(size), &returned, 0) >= 0,
            "SHA-256 setup failed", ErrorCode::Io);
    h.object.resize(size);
    require(BCryptCreateHash(h.algorithm, &h.hash, h.object.data(), size, nullptr, 0, 0) >= 0,
            "SHA-256 setup failed", ErrorCode::Io);
    while (f) {
        if (beforeRead)
            beforeRead();
        f.read(buffer.data(), buffer.size());
        require(BCryptHashData(h.hash, reinterpret_cast<PUCHAR>(buffer.data()),
                               static_cast<ULONG>(f.gcount()), 0) >= 0,
                "SHA-256 update failed", ErrorCode::Io);
    }
    require(f.eof(), "Media read failed", ErrorCode::Io);
    require(BCryptFinishHash(h.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0,
            "SHA-256 finalization failed", ErrorCode::Io);
    // Destroy while the caller-owned hash-object buffer still exists.
    BCryptDestroyHash(h.hash);
    h.hash = nullptr;
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> h(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(h && EVP_DigestInit_ex(h.get(), EVP_sha256(), nullptr) == 1, "SHA-256 unavailable",
            ErrorCode::Io);
    while (f) {
        if (beforeRead)
            beforeRead();
        f.read(buffer.data(), buffer.size());
        require(EVP_DigestUpdate(h.get(), buffer.data(), static_cast<std::size_t>(f.gcount())) == 1,
                "SHA-256 update failed", ErrorCode::Io);
    }
    require(f.eof(), "Media read failed", ErrorCode::Io);
    unsigned size = 0;
    require(EVP_DigestFinal_ex(h.get(), digest.data(), &size) == 1 && size == digest.size(),
            "SHA-256 finalization failed", ErrorCode::Io);
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (auto b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
void ProjectStore::verifyMedia(const Session &s, const std::function<void()> &beforeRead) const {
    const ValidatedSession checked(s, budget_.state);
    noLink(root_);
    require(std::filesystem::is_directory(root_), "Project directory unavailable", ErrorCode::Io);
    for (const auto &a : s.assets) {
        auto p = root_;
        const auto relative = utf8Path(a.relativePath);
        for (const auto &part : relative) {
            p /= part;
            noLink(p);
        }
        require(hashMediaFile(p, beforeRead) == a.sha256, "Media hash mismatch",
                ErrorCode::MediaMismatch);
    }
    const auto evidence = [&](const ProjectEvidenceFile &f) {
        auto path = root_;
        for (const auto &part : utf8Path(f.relativePath)) { path /= part; noLink(path); }
        require(plainFile(path), "Imported evidence missing or not a plain file", ErrorCode::MissingMedia);
        require(std::filesystem::file_size(path) == f.bytes, "Imported evidence size differs",
                ErrorCode::MediaMismatch);
        std::uint64_t reads = 0;
        const auto maximumReads = f.bytes / 65536 + 1;
        const auto digest = hashMediaFile(path, [&] {
            require(++reads <= maximumReads, "Imported evidence grew during bounded hashing",
                    ErrorCode::MediaMismatch);
            if (beforeRead) beforeRead();
        });
        require(digest == f.sha256 && std::filesystem::file_size(path) == f.bytes,
                "Imported evidence checksum differs", ErrorCode::MediaMismatch);
    };
    for (const auto &source : s.imports) {
        evidence(source.inspection);
        for (const auto &origin : source.media) evidence(origin.receipt);
    }
    if (!s.imports.empty()) {
#ifdef SC_STORE_IMPORT_STATE
        ResourceLedger memory(budget_.importEvidenceBytes, "Project import evidence verification");
        for (const auto &source : s.imports) {
            if (beforeRead) beforeRead();
            auto verified = openProjectImportEvidence(root_, checked, source.id, memory, {}, {}, beforeRead);
            (void)verified;
        }
#else
        fail("Import evidence verification requires media support", ErrorCode::UnsupportedSchema);
#endif
    }
}
SaveResult ProjectStore::save(const Session &s, const SaveOptions &options) const {
    const auto encoded = encodeProject(s, budget_);
    noLink(root_);
    std::filesystem::create_directories(root_);
    WriterLock lock(root_);
    verifyMedia(s);
    const auto current = root_ / "project.json";
    bool previous = false;
    noLink(current);
    if (std::filesystem::exists(current)) {
        auto old = readFile(current, budget_);
        const auto prior = decodeProject(old, budget_);
        require(prior.id == s.id, "Refusing to overwrite another project");
        publish(root_ / "project.previous.json", old);
        previous = true;
    }
    return {previous, publish(current, encoded, options)};
}
Session ProjectStore::loadFile(const std::filesystem::path &p) const {
    auto s = decodeProject(readFile(p, budget_), budget_);
    verifyMedia(s);
    return s;
}
Session ProjectStore::load() const {
    return loadFile(root_ / "project.json");
}
Session ProjectStore::loadPrevious() const {
    return loadFile(root_ / "project.previous.json");
}
} // namespace soundcurrent::daw
