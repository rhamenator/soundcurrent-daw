// SPDX-License-Identifier: GPL-3.0-only
// Isolated off-RT RF64/flush/header-update probe; not the capture writer.
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Descriptor {
    int fd = -1;
    ~Descriptor() {
        if (fd >= 0)
            close(fd);
    }
};
struct SoundFile {
    SNDFILE *file = nullptr;
    ~SoundFile() {
        if (file)
            sf_close(file);
    }
};
std::vector<float> read(const std::filesystem::path &path, sf_count_t frames) {
    SF_INFO info{};
    SoundFile reader{sf_open(path.c_str(), SFM_READ, &info)};
    check(reader.file && info.frames == frames && info.channels == 1 && info.samplerate == 48000,
          "Header extent/rate/layout mismatch");
    std::vector<float> result(static_cast<std::size_t>(frames));
    check(sf_readf_float(reader.file, result.data(), frames) == frames, "Cannot read exact prefix");
    return result;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 2, "Supply a new output path");
        const std::filesystem::path path(argv[1]);
        Descriptor descriptor{
            open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600)};
        check(descriptor.fd >= 0, "Cannot exclusively create probe file");
        SF_INFO info{};
        info.samplerate = 48000;
        info.channels = 1;
        info.format = SF_FORMAT_RF64 | SF_FORMAT_FLOAT;
        check(sf_format_check(&info) == SF_TRUE, "RF64 float format unsupported");
        SoundFile writer{sf_open_fd(descriptor.fd, SFM_WRITE, &info, SF_FALSE)};
        check(writer.file, "Cannot open RF64 writer");
        std::vector<float> samples(1536);
        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>((double(i % 101) - 50) * .04);
        check(sf_writef_float(writer.file, samples.data(), 1024) == 1024, "Prefix write failed");
        check(sf_command(writer.file, SFC_UPDATE_HEADER_NOW, nullptr, 0) == 0,
              "Checkpoint header update failed");
        sf_write_sync(writer.file);
        check(sf_error(writer.file) == SF_ERR_NO_ERROR && fsync(descriptor.fd) == 0,
              "Prefix flush failed");
        auto prefix = read(path, 1024);
        check(std::equal(prefix.begin(), prefix.end(), samples.begin()),
              "Reopened active-writer prefix differs");
        check(sf_writef_float(writer.file, samples.data() + 1024, 512) == 512,
              "Resume write failed");
        check(sf_close(writer.file) == 0, "Final close failed");
        writer.file = nullptr;
        check(fsync(descriptor.fd) == 0, "Final descriptor flush failed");
        auto result = read(path, 1536);
        check(result == samples, "RF64 float reopen differs or clips overs");
        std::array<char, 4> signature{};
        check(pread(descriptor.fd, signature.data(), signature.size(), 0) == 4 &&
                  signature == std::array<char, 4>{'R', 'F', '6', '4'},
              "File is not RF64");
        std::cout
            << "{\"runtime\":\"" << sf_version_string()
            << "\",\"checkpoint_frames\":1024,\"final_frames\":1536,\"float_overs_preserved\":true,"
               "\"active_writer_prefix_reopened\":true,\"signature\":\"RF64\"}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
