// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <soundcurrent/project_store.hpp>
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
    return {{"backendId", r.backendId}, {"portIdentity", r.portIdentity}};
}
RouteIntent readRoute(const Json &j) {
    keys(j, {"backendId", "portIdentity"});
    return {string(j.at("backendId")), string(j.at("portIdentity"))};
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
std::string readFile(const std::filesystem::path &p) {
    require(plainFile(p), "Project file missing or not a regular file", ErrorCode::Io);
    require(std::filesystem::file_size(p) <= maxProjectBytes, "Project size limit exceeded");
    std::ifstream f(p, std::ios::binary);
    require(bool(f), "Cannot read project", ErrorCode::Io);
    std::string out;
    std::array<char, 8192> buffer{};
    while (f) {
        f.read(buffer.data(), buffer.size());
        out.append(buffer.data(), static_cast<std::size_t>(f.gcount()));
        require(out.size() <= maxProjectBytes, "Project size limit exceeded");
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
std::filesystem::path utf8Path(std::string_view s) {
    require(validUtf8(s) && s.find('\0') == s.npos, "Invalid UTF-8 path");
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}
std::string encodeProject(const Session &s) {
    validate(s);
    Json tracks = Json::array(), assets = Json::array();
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
                             {"lengthFrames", c.lengthFrames}});
        Json processor = {{"id", t.eq.id.str()},
                          {"type", "sc.eq"},
                          {"version", 1},
                          {"enabled", t.eq.enabled},
                          {"bands", bands}};
        tracks.push_back({{"id", t.id.str()},
                          {"name", t.name},
                          {"layout", layout(t.layout)},
                          {"inputIntent", route(t.input)},
                          {"outputIntent", route(t.output)},
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
        {"schemaMinor", 0},
        {"projectId", s.id.str()},
        {"name", s.name},
        {"sampleRate", s.sampleRate},
        {"playheadFrame", s.playheadFrame},
        {"exportRange", {{"startFrame", s.exportStartFrame}, {"endFrame", s.exportEndFrame}}},
        {"tracks", tracks},
        {"assets", assets}};
    auto out = root.dump(2) + "\n";
    require(out.size() <= maxProjectBytes, "Project size limit exceeded");
    return out;
}
Session decodeProject(std::string_view bytes) {
    require(bytes.size() <= maxProjectBytes, "Project size limit exceeded");
    try {
        std::vector<std::unordered_set<std::string>> objectKeys;
        auto callback = [&](int depth, Json::parse_event_t event, Json &value) {
            require(depth <= 32, "Project nesting limit exceeded");
            if (event == Json::parse_event_t::object_start)
                objectKeys.emplace_back();
            else if (event == Json::parse_event_t::key) {
                require(!objectKeys.empty() &&
                            objectKeys.back().insert(value.get<std::string>()).second,
                        "Duplicate JSON key");
            } else if (event == Json::parse_event_t::object_end)
                objectKeys.pop_back();
            return true;
        };
        const auto j = Json::parse(bytes.begin(), bytes.end(), callback);
        require(j.is_object() && j.contains("schemaMajor") && j.contains("schemaMinor"),
                "Missing project schema");
        require(integer(j.at("schemaMajor")) == 1 && integer(j.at("schemaMinor")) == 0,
                "Unsupported project schema", ErrorCode::UnsupportedSchema);
        keys(j, {"format", "schemaMajor", "schemaMinor", "projectId", "name", "sampleRate",
                 "playheadFrame", "exportRange", "tracks", "assets"});
        require(string(j.at("format")) == "soundcurrent-daw", "Unrecognized project format");
        Session s;
        s.id = Id(string(j.at("projectId")));
        s.name = string(j.at("name"));
        s.sampleRate = u32(j.at("sampleRate"));
        s.playheadFrame = integer(j.at("playheadFrame"));
        const auto &range = j.at("exportRange");
        keys(range, {"startFrame", "endFrame"});
        s.exportStartFrame = integer(range.at("startFrame"));
        s.exportEndFrame = integer(range.at("endFrame"));
        array(j.at("assets"), 4096);
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
        array(j.at("tracks"), 256);
        for (const auto &t : j.at("tracks")) {
            keys(t, {"id", "name", "layout", "inputIntent", "outputIntent", "processors", "clips"});
            Track track;
            track.id = Id(string(t.at("id")));
            track.name = string(t.at("name"));
            track.layout = readLayout(t.at("layout"));
            track.input = readRoute(t.at("inputIntent"));
            track.output = readRoute(t.at("outputIntent"));
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
            for (const auto &b : p.at("bands")) {
                keys(b, {"id", "frequencyHz", "gainDb", "q"});
                EqBand band;
                band.id = Id(string(b.at("id")));
                band.frequencyHz = number(b.at("frequencyHz"));
                band.gainDb = number(b.at("gainDb"));
                band.q = number(b.at("q"));
                track.eq.bands.push_back(std::move(band));
            }
            array(t.at("clips"), 8192);
            for (const auto &c : t.at("clips")) {
                keys(c, {"id", "assetId", "startFrame", "sourceFrame", "lengthFrames"});
                Clip clip;
                clip.id = Id(string(c.at("id")));
                clip.assetId = Id(string(c.at("assetId")));
                clip.startFrame = integer(c.at("startFrame"));
                clip.sourceFrame = integer(c.at("sourceFrame"));
                clip.lengthFrames = integer(c.at("lengthFrames"));
                track.clips.push_back(std::move(clip));
            }
            s.tracks.push_back(std::move(track));
        }
        validate(s);
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
    validate(s);
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
}
SaveResult ProjectStore::save(const Session &s, const SaveOptions &options) const {
    const auto encoded = encodeProject(s);
    noLink(root_);
    std::filesystem::create_directories(root_);
    WriterLock lock(root_);
    verifyMedia(s);
    const auto current = root_ / "project.json";
    bool previous = false;
    noLink(current);
    if (std::filesystem::exists(current)) {
        auto old = readFile(current);
        const auto prior = decodeProject(old);
        require(prior.id == s.id, "Refusing to overwrite another project");
        publish(root_ / "project.previous.json", old);
        previous = true;
    }
    return {previous, publish(current, encoded, options)};
}
Session ProjectStore::loadFile(const std::filesystem::path &p) const {
    auto s = decodeProject(readFile(p));
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
