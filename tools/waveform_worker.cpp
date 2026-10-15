// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/waveform_protocol.hpp>
#include <algorithm>
#include <charconv>
#include <iostream>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
using namespace soundcurrent::daw;
namespace {
void require(bool good) {
    if (!good) throw ProjectError(ErrorCode::InvalidParameter, "Invalid waveform worker request");
}
std::uint64_t number(const std::string &s) {
    std::uint64_t n = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(!s.empty() && r.ec == std::errc{} && r.ptr == s.data() + s.size()); return n;
}
int run(const std::vector<std::string> &args) {
    try {
        // Strict bounded command schema. Only the supervising application chooses
        // these limits; untrusted project fields must not select a larger policy.
        constexpr std::array<std::string_view, 12> flags{
            "--root", "--relative", "--sha256", "--frames", "--rate", "--channels",
            "--base-frames", "--operation", "--epoch", "--generation", "--maximum-bytes", "--memory-bytes"};
        require(args.size() == flags.size() * 2 + 1);
        for (std::size_t i = 0; i < flags.size(); ++i) {
            require(args[1 + 2 * i] == flags[i]);
            require(args[2 + 2 * i].size() <= (i < 2 ? 4096 : 64));
        }
        const auto value = [&](std::size_t i) -> const std::string & { return args[2 + 2 * i]; };
        require(!value(0).empty() && validUtf8(value(0)) && value(0).find('\0') == std::string::npos);
        const auto rootPath = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(value(0).data()), value(0).size()));
        // ApprovedMediaRoot validates portable paths relative to this explicitly
        // approved root. A project asset's mandatory "media/" prefix does not
        // apply when the approved root is already the media directory.
        require(rootPath.is_absolute());
        require(value(2).size() == 64);
        const auto rate = number(value(4)), channels = number(value(5));
        require(rate <= UINT32_MAX && channels <= UINT32_MAX);
        WaveformTicket ticket{Id(value(7)), number(value(8)), number(value(9)),
            {{}, number(value(3)), static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(channels)}, number(value(6))};
        std::copy(value(2).begin(), value(2).end(), ticket.source.sha256.begin());
        WaveformLimits limits; limits.baseFramesPerBin = ticket.baseFramesPerBin;
        const auto maximum = number(value(10)), memory = number(value(11));
        require(maximum && maximum <= 8ULL * 1024 * 1024 * 1024 &&
                memory >= 1024 * 1024 && memory <= 256ULL * 1024 * 1024);
        (void)waveformReplySize(ticket, limits); // Validate complete geometry before opening media.
        ResourceLedger ledger(static_cast<std::size_t>(memory), "Waveform worker payload");
        ApprovedMediaRoot root(rootPath, ledger, {1}); auto file = root.open(value(1), maximum);
        auto wave = buildApprovedWaveform(file, ticket.source, limits);
#ifdef _WIN32
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = getpid();
#endif
        auto reply = encodeWaveformReply(wave, ticket, static_cast<std::uint64_t>(pid), ledger, limits);
        // No stdout before the whole source and reply are validated. The parent
        // must still require exact bytes AND normal exit: a pipe write can fail.
        std::cout.write(reply.bytes().data(), static_cast<std::streamsize>(reply.bytes().size()));
        std::cout.flush(); return std::cout ? 0 : 1;
    } catch (const ProjectError &e) {
        std::cerr << "sc-waveform-v1:error:" << static_cast<unsigned>(e.code()) << '\n'; return 1;
    } catch (const std::exception &) {
        std::cerr << "sc-waveform-v1:failed\n"; return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 1;
    if (argc != 25) return 1;
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0 || size > 4097) return 1;
        std::string s(static_cast<std::size_t>(size), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, s.data(), size, nullptr, nullptr)) return 1;
        s.pop_back(); args.push_back(std::move(s));
    }
    return run(args);
}
#else
int main(int argc, char **argv) {
    if (argc != 25) return 1;
    for (int i = 0; i < argc; ++i) if (std::string_view(argv[i]).size() > 4096) return 1;
    return run({argv, argv + argc});
}
#endif
