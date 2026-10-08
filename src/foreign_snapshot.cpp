// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/foreign_snapshot.hpp>
#include <algorithm>
#include <memory>
#include <span>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace soundcurrent::daw {
namespace {
void require(bool condition, const char *message, ErrorCode code = ErrorCode::Io) {
    if (!condition) throw ProjectError(code, message);
}
void poll(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Foreign snapshot canceled");
}
// One pinned read-only handle. Reject special files and final-component links
// before reading; never follow tokens contained inside the foreign project.
class InputFile {
  public:
    explicit InputFile(const std::filesystem::path &path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        require(handle_ != INVALID_HANDLE_VALUE, "Cannot open selected input");
#else
        handle_ = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        require(handle_ >= 0, "Cannot open selected input");
#endif
        // Constructor refusal must close the already acquired handle itself.
        try {
            initial_ = information();
        } catch (...) {
            closeHandle();
            throw;
        }
    }
    ~InputFile() { closeHandle(); }
    InputFile(const InputFile &) = delete;
    InputFile &operator=(const InputFile &) = delete;
    std::uint64_t size() const noexcept { return initial_.size; }
    std::size_t read(std::span<char> target) {
        require(target.size() <= 65536, "Input read exceeds worker chunk", ErrorCode::InvalidState);
#ifdef _WIN32
        DWORD count = 0;
        require(ReadFile(handle_, target.data(), static_cast<DWORD>(target.size()), &count, nullptr),
                "Cannot read selected input");
        return count;
#else
        const auto count = ::read(handle_, target.data(), target.size());
        if (count < 0 && errno == EINTR)
            return interrupted;
        require(count >= 0, "Cannot read selected input");
        return static_cast<std::size_t>(count);
#endif
    }
    void verifyMetadata() const {
        require(information() == initial_, "Input metadata changed during inspection");
    }
    static constexpr auto interrupted = std::numeric_limits<std::size_t>::max();
  private:
    struct Information {
        std::uint64_t size = 0, modified = 0, extraModified = 0;
        bool operator==(const Information &) const = default;
    } initial_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
    Information information() const {
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION info{};
        require(GetFileType(handle_) == FILE_TYPE_DISK && GetFileInformationByHandle(handle_, &info),
                "Input is not a regular disk file");
        require(!(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
                "Input is not a plain file");
        return {(std::uint64_t(info.nFileSizeHigh)<<32) | info.nFileSizeLow,
                (std::uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32) |
                    info.ftLastWriteTime.dwLowDateTime, 0};
#else
        struct stat info{};
        require(fstat(handle_, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0,
                "Input is not a regular disk file");
        return {static_cast<std::uint64_t>(info.st_size),
                static_cast<std::uint64_t>(info.st_mtim.tv_sec),
                static_cast<std::uint64_t>(info.st_mtim.tv_nsec)};
#endif
    }
    void closeHandle() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (handle_ >= 0) {
            close(handle_);
            handle_ = -1;
        }
#endif
    }
};
} // namespace
ForeignSnapshot readForeignSnapshot(const std::filesystem::path &path, std::size_t maximumBytes,
                                      ResourceLedger resources, std::stop_token stop) {
    poll(stop);
    require(maximumBytes>0,"Invalid trusted snapshot limit",ErrorCode::InvalidParameter);
    InputFile file(path);
    if (file.size() > maximumBytes)
        throw ResourceLimitError("Import input bytes", static_cast<std::size_t>(
            std::min<std::uint64_t>(file.size(), std::numeric_limits<std::size_t>::max())),
            maximumBytes);
    const auto size = static_cast<std::size_t>(file.size());
    PayloadCharge charge("Import input staging", resources.usage().limitBytes);
    charge.add(sizeof(ForeignSnapshot)); charge.add(size); charge.add(1);
    ForeignSnapshot input(resources.reserve(charge.bytes()),size);
    for (std::size_t position = 0; position < size;) {
        poll(stop);
        const auto count = file.read(std::span(input.bytes_).subspan(
            position, std::min<std::size_t>(65536, size-position)));
        if (count == InputFile::interrupted)
            continue;
        require(count != 0, "Selected input truncated during read");
        position += count;
    }
    char extra = 0;
    std::size_t count = 0;
    do {
        poll(stop);
        count = file.read({&extra, 1});
    } while (count == InputFile::interrupted);
    require(count == 0, "Selected input grew during read");
    file.verifyMetadata();
    return input;
}

std::array<char, 64> hashForeignSnapshot(std::string_view bytes, ResourceLedger resources,
                                std::stop_token stop) {
    // Existing platform SHA-256 providers. This allowance covers known object
    // banks, not hidden provider allocations or a hard process RSS limit.
    auto cryptoCredit = resources.reserve(65536);
    std::array<unsigned char, 32> digest{};
#ifdef _WIN32
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<unsigned char> object;
        ~Hash() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } h;
    require(BCryptOpenAlgorithmProvider(&h.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
            "SHA-256 provider unavailable");
    DWORD length = 0, returned = 0;
    require(BCryptGetProperty(h.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
                              sizeof(length), &returned, 0) >= 0 && length <= 65536,
            "SHA-256 object exceeds admitted allowance");
    h.object.resize(length);
    require(BCryptCreateHash(h.algorithm, &h.hash, h.object.data(), length, nullptr, 0, 0) >= 0,
            "SHA-256 initialization failed");
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> h(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(h && EVP_DigestInit_ex(h.get(), EVP_sha256(), nullptr) == 1,
            "SHA-256 provider unavailable");
#endif
    for (std::size_t position = 0; position < bytes.size();) {
        poll(stop);
        const auto block = bytes.substr(position, std::min<std::size_t>(65536, bytes.size()-position));
#ifdef _WIN32
        require(BCryptHashData(h.hash, reinterpret_cast<PUCHAR>(const_cast<char *>(block.data())),
                               static_cast<ULONG>(block.size()), 0) >= 0, "SHA-256 update failed");
#else
        require(EVP_DigestUpdate(h.get(), block.data(), block.size()) == 1, "SHA-256 update failed");
#endif
        position += block.size();
    }
#ifdef _WIN32
    require(BCryptFinishHash(h.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0,
            "SHA-256 finalization failed");
#else
    unsigned length = 0;
    require(EVP_DigestFinal_ex(h.get(), digest.data(), &length) == 1 && length == digest.size(),
            "SHA-256 finalization failed");
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 64> output{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        output[2*i] = hex[digest[i]>>4];
        output[2*i+1] = hex[digest[i]&15];
    }
    return output;
}
} // namespace soundcurrent::daw
