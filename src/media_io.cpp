// SPDX-License-Identifier: GPL-3.0-only
#include "media_io.hpp"
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#endif

namespace soundcurrent::daw::media_io {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
                  std::endian::native == std::endian::little,
              "Sample digest version 1 requires little-endian IEEE float32");
void require(bool ok, const char *message) {
    if (!ok)
        throw ProjectError(ErrorCode::Io, message);
}
void plainDirectory(const std::filesystem::path &p) {
    require(std::filesystem::is_directory(std::filesystem::symlink_status(p)),
            "Media directory missing or linked");
#ifdef _WIN32
    const auto a = GetFileAttributesW(p.c_str());
    require(a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT),
            "Media directory reparse point refused");
#endif
}
void plainFile(const std::filesystem::path &p) {
    require(std::filesystem::is_regular_file(std::filesystem::symlink_status(p)),
            "Media file missing or linked");
#ifdef _WIN32
    const auto a = GetFileAttributesW(p.c_str());
    require(a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_REPARSE_POINT),
            "Media reparse point refused");
#endif
}
File::File(const std::filesystem::path &p, bool create) {
    if (!create)
        plainFile(p);
#ifdef _WIN32
    const auto h = CreateFileW(p.c_str(), create ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                               FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(h != INVALID_HANDLE_VALUE, "Cannot open media descriptor");
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        (tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
        CloseHandle(h);
        require(false, "Unsafe media descriptor");
    }
    fd_ =
        _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_BINARY | (create ? _O_RDWR : _O_RDONLY));
    if (fd_ < 0)
        CloseHandle(h);
#else
    fd_ = open(p.c_str(),
               create ? O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW
                      : O_RDONLY | O_CLOEXEC | O_NOFOLLOW,
               0600);
#endif
    require(fd_ >= 0, "Cannot exclusively create/read media");
}
File::~File() {
    if (fd_ >= 0) {
#ifdef _WIN32
        _close(fd_);
#else
        ::close(fd_);
#endif
    }
}
void File::close() {
    const auto fd = fd_;
    fd_ = -1;
#ifdef _WIN32
    require(fd < 0 || _close(fd) == 0, "Media descriptor close failed");
#else
    require(fd < 0 || ::close(fd) == 0, "Media descriptor close failed");
#endif
}
void File::flush() {
#ifdef _WIN32
    require(FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(fd_))) != 0,
            "Media flush failed");
#else
    require(fsync(fd_) == 0, "Media flush failed");
#endif
}
void File::write(std::string_view bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
#ifdef _WIN32
        const auto n =
            _write(fd_, bytes.data() + offset, static_cast<unsigned>(bytes.size() - offset));
#else
        const auto n = ::write(fd_, bytes.data() + offset, bytes.size() - offset);
        if (n < 0 && errno == EINTR)
            continue;
#endif
        require(n > 0, "Journal write failed");
        offset += static_cast<std::size_t>(n);
    }
}
Durability flushDirectory(const std::filesystem::path &p) {
#ifdef _WIN32
    (void)p;
    return Durability::FileFlushed;
#else
    const int fd = open(p.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    require(fd >= 0, "Cannot open media directory for flush");
    const bool ok = fsync(fd) == 0;
    ::close(fd);
    require(ok, "Media directory flush failed after publication");
    return Durability::FileAndDirectoryFlushed;
#endif
}
void publishMedia(const std::filesystem::path &source, const std::filesystem::path &destination) {
    plainFile(source);
    require(source.parent_path() == destination.parent_path(), "Cross-directory media publication");
#ifdef _WIN32
    require(MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0,
            "Cannot publish media without overwriting");
#else
    require(link(source.c_str(), destination.c_str()) == 0,
            "Cannot publish media without overwriting");
    require(unlink(source.c_str()) == 0, "Published media; partial alias cleanup failed");
#endif
    flushDirectory(destination.parent_path());
}
Durability publishJournal(const std::filesystem::path &destination, std::string_view bytes) {
    require(bytes.size() <= 16384, "Journal size limit exceeded");
    if (std::filesystem::exists(std::filesystem::symlink_status(destination)))
        plainFile(destination);
    const auto temp = destination.parent_path() / ("journal-" + Id::generate().str() + ".partial");
    bool owned = false;
    try {
        File file(temp, true);
        owned = true;
        file.write(bytes);
        file.flush();
        file.close();
#ifdef _WIN32
        require(MoveFileExW(temp.c_str(), destination.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "Cannot publish recording journal");
#else
        require(rename(temp.c_str(), destination.c_str()) == 0, "Cannot publish recording journal");
#endif
        owned = false;
        return flushDirectory(destination.parent_path());
    } catch (...) {
        if (owned) {
            std::error_code error;
            std::filesystem::remove(temp, error);
        }
        throw;
    }
}
std::string readJournal(const std::filesystem::path &p) {
    plainFile(p);
    require(std::filesystem::file_size(p) <= 16384, "Journal size limit exceeded");
    std::ifstream stream(p, std::ios::binary);
    require(bool(stream), "Cannot read recording journal");
    std::string result;
    std::array<char, 1024> bytes{};
    while (stream) {
        stream.read(bytes.data(), bytes.size());
        result.append(bytes.data(), static_cast<std::size_t>(stream.gcount()));
        require(result.size() <= 16384, "Journal size limit exceeded");
    }
    require(stream.eof(), "Journal read failed");
    return result;
}
struct JobLease::State {
    LeaseStatus status = LeaseStatus::Absent;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~State() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    int fd = -1;
    ~State() {
        if (fd >= 0)
            ::close(fd);
    }
#endif
};
JobLease::JobLease(const std::filesystem::path &job, bool writer)
    : state_(std::make_unique<State>()) {
    plainDirectory(job);
    const auto path = job / "writer.lock";
    if (!writer && !std::filesystem::exists(std::filesystem::symlink_status(path)))
        return;
    if (!writer)
        plainFile(path);
#ifdef _WIN32
    auto &s = *state_;
    s.handle =
        CreateFileW(path.c_str(), writer ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                    writer ? 0 : FILE_SHARE_READ, nullptr, writer ? CREATE_NEW : OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (s.handle == INVALID_HANDLE_VALUE) {
        if (!writer && GetLastError() == ERROR_SHARING_VIOLATION) {
            s.status = LeaseStatus::Busy;
            return;
        }
        require(false, "Cannot acquire recording job lease");
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    require(GetFileInformationByHandleEx(s.handle, FileAttributeTagInfo, &tag, sizeof(tag)) &&
                !(tag.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)),
            "Unsafe recording lease file");
#else
    auto &s = *state_;
    s.fd = open(path.c_str(),
                writer ? O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW
                       : O_RDONLY | O_CLOEXEC | O_NOFOLLOW,
                0600);
    require(s.fd >= 0, "Cannot open recording job lease");
    struct stat info{};
    require(fstat(s.fd, &info) == 0 && S_ISREG(info.st_mode), "Unsafe recording lease file");
    if (flock(s.fd, (writer ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
        if (!writer && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            s.status = LeaseStatus::Busy;
            return;
        }
        require(false, "Cannot acquire recording job lease");
    }
#endif
    state_->status = LeaseStatus::Held;
}
JobLease::~JobLease() = default;
LeaseStatus JobLease::status() const noexcept {
    return state_->status;
}
struct SampleHash::State {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    ~State() {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
#else
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    ~State() {
        EVP_MD_CTX_free(context);
    }
#endif
};
SampleHash::SampleHash() : state_(std::make_unique<State>()) {
#ifdef _WIN32
    auto &s = *state_;
    require(BCryptOpenAlgorithmProvider(&s.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "Capture SHA-256 unavailable");
    DWORD size = 0, returned = 0;
    require(BCryptGetProperty(s.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                              sizeof(size), &returned, 0) >= 0,
            "Capture SHA-256 setup failed");
    s.object.resize(size);
    require(BCryptCreateHash(s.algorithm, &s.hash, s.object.data(), size, nullptr, 0, 0) >= 0,
            "Capture SHA-256 setup failed");
#else
    require(state_->context && EVP_DigestInit_ex(state_->context, EVP_sha256(), nullptr) == 1,
            "Capture SHA-256 setup failed");
#endif
}
SampleHash::~SampleHash() = default;
void SampleHash::update(std::span<const float> samples) {
    updateBytes(std::as_bytes(samples));
}
void SampleHash::updateBytes(std::span<const std::byte> bytes) {
#ifdef _WIN32
    require(bytes.size() <= std::numeric_limits<ULONG>::max() &&
                BCryptHashData(state_->hash,
                               reinterpret_cast<PUCHAR>(const_cast<std::byte *>(bytes.data())),
                               static_cast<ULONG>(bytes.size()), 0) >= 0,
            "Capture SHA-256 update failed");
#else
    require(EVP_DigestUpdate(state_->context, bytes.data(), bytes.size()) == 1,
            "Capture SHA-256 update failed");
#endif
}
std::string File::digest(const std::function<void()> &beforeRead) {
#ifdef _WIN32
    require(_lseeki64(fd_, 0, SEEK_SET) == 0, "Media hash seek failed");
#else
    require(lseek(fd_, 0, SEEK_SET) == 0, "Media hash seek failed");
#endif
    SampleHash hash;
    std::array<std::byte, 65536> buffer;
    for (;;) {
        if (beforeRead)
            beforeRead();
#ifdef _WIN32
        const auto count = _read(fd_, buffer.data(), static_cast<unsigned>(buffer.size()));
#else
        const auto count = ::read(fd_, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
#endif
        require(count >= 0, "Media hash read failed");
        if (!count)
            break;
        hash.updateBytes({buffer.data(), static_cast<std::size_t>(count)});
    }
#ifdef _WIN32
    require(_lseeki64(fd_, 0, SEEK_SET) == 0, "Media hash rewind failed");
#else
    require(lseek(fd_, 0, SEEK_SET) == 0, "Media hash rewind failed");
#endif
    return hash.digest();
}
std::string SampleHash::digest() const {
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    State copy;
    copy.object.resize(state_->object.size());
    require(BCryptDuplicateHash(state_->hash, &copy.hash, copy.object.data(),
                                static_cast<ULONG>(copy.object.size()), 0) >= 0 &&
                BCryptFinishHash(copy.hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) >= 0,
            "Capture SHA-256 snapshot failed");
#else
    const auto ctx =
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    unsigned length = 0;
    require(ctx && EVP_MD_CTX_copy_ex(ctx.get(), state_->context) == 1 &&
                EVP_DigestFinal_ex(ctx.get(), bytes.data(), &length) == 1 && length == bytes.size(),
            "Capture SHA-256 snapshot failed");
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        result[i * 2] = hex[bytes[i] >> 4];
        result[i * 2 + 1] = hex[bytes[i] & 15];
    }
    return result;
}
} // namespace soundcurrent::daw::media_io
