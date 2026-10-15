// SPDX-License-Identifier: GPL-3.0-only
// Deliberately faulty test executable, never installed or chosen by project data.
#include <array>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char **argv) {
#ifdef _WIN32
    // Test modes specify exact diagnostic bytes, including an explicit CRLF.
    // Prevent the Windows CRT from translating that LF into a second CRLF.
    if (_setmode(_fileno(stdout), _O_BINARY) == -1 ||
        _setmode(_fileno(stderr), _O_BINARY) == -1) return 1;
#endif
    std::string_view mode;
    for (int i = 1; i + 1 < argc; ++i) if (std::string_view(argv[i]) == "--maximum-bytes") mode = argv[i + 1];
    if (mode == "100001") for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (mode == "100002" || mode == "100003") {
        std::array<char, 4096> bank{};
        auto &out = mode == "100002" ? std::cout : std::cerr;
        for (;;) { out.write(bank.data(), std::streamsize(bank.size())); out.flush(); if (!out) return 1; }
    }
    if (mode == "100004") { std::cout << "SCWAVE01"; return 0; }
    if (mode == "100005") { std::cerr << "sc-waveform-v1:error:5\r\n"; return 1; }
    if (mode == "100006") { std::cerr << "untrusted diagnostic sc-waveform-v1:error:5\n"; return 1; }
    return 7;
}
