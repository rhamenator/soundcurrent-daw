// SPDX-License-Identifier: GPL-3.0-only
// Separate-process inspection protocol; never linked into an audio callback.
#include <soundcurrent/reaper_structure.hpp>
#include <soundcurrent/foreign_snapshot.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace soundcurrent::daw;
namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> quitRequested{false};
void signalQuit(int) { quitRequested.store(true, std::memory_order_relaxed); }
void require(bool condition, const char *message, ErrorCode code = ErrorCode::Io) {
    if (!condition)
        throw ProjectError(code, message);
}
void poll(std::stop_source &stop) {
    if (quitRequested.load(std::memory_order_relaxed))
        stop.request_stop();
    if (stop.stop_requested())
        throw ProjectError(ErrorCode::Canceled, "Import inspection canceled");
}
std::size_t unsignedOption(std::string_view value) {
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data()+value.size(), result);
    require(error == std::errc{} && end == value.data()+value.size() && result,
            "Invalid trusted inspection limit", ErrorCode::InvalidParameter);
    return result;
}

// ASCII protocol, numeric ranges only. Foreign text is never interpolated into
// JSON, interpreted as Unicode, or leaked through error diagnostics.
class Chunk {
  public:
    Chunk &text(std::string_view text) {
        require(text.size() <= bytes_.size()-length_, "Report row exceeds fixed bank", ErrorCode::InvalidState);
        std::copy(text.begin(), text.end(), bytes_.begin()+length_);
        length_ += text.size();
        return *this;
    }
    Chunk &number(std::size_t value) {
        const auto result = std::to_chars(bytes_.data()+length_, bytes_.data()+bytes_.size(), value);
        require(result.ec == std::errc{}, "Report number exceeds fixed bank", ErrorCode::InvalidState);
        length_ = static_cast<std::size_t>(result.ptr-bytes_.data());
        return *this;
    }
    Chunk &range(ForeignByteRange range) {
        return text("[").number(range.begin).text(",").number(range.length).text("]");
    }
    std::string_view bytes() const noexcept { return {bytes_.data(), length_}; }
  private:
    std::array<char, 2048> bytes_{};
    std::size_t length_ = 0;
};
void report(const ReaperStructure &doc, std::string_view hash, std::size_t maximum,
            std::ostream *output, std::stop_source &stop) {
    PayloadCharge charge("Import report bytes", maximum);
    auto put = [&](std::string_view bytes) {
        poll(stop);
        charge.add(bytes.size());
        if (output) {
            output->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            require(bool(*output), "Import report write failed");
        }
    };
#ifdef _WIN32
    const auto pid = static_cast<std::size_t>(GetCurrentProcessId());
#else
    const auto pid = static_cast<std::size_t>(getpid());
#endif
    const auto header = Chunk{}.text("{\"protocol\":\"sc-import-inspection-v1\",\"adapter\":\"rpp-outline-v1\","
        "\"sourceFormat\":\"reaper-rpp\",\"workerPid\":").number(pid)
        .text(",\"source\":{\"bytes\":").number(doc.source().size()).text(",\"sha256\":\"").text(hash)
        .text("\",\"storage\":\"worker-memory-snapshot\",\"consistency\":\"size-and-mtime-checked-not-atomic\"},"
              "\"nativeCompatibility\":\"unqualified\",\"semanticStatus\":\"unverified\","
              "\"writerVersion\":{\"status\":\"unverified\",\"headerRange\":")
        .range(doc.nodes()[doc.root()].line)
        .text("},\"root\":").number(doc.root()).text(",\"nodes\":[");
    put(header.bytes());
    for (std::size_t i = 0; i < doc.nodes().size(); ++i) {
        const auto &node = doc.nodes()[i];
        std::string_view kind;
        switch (node.kind) {
        case ReaperLineKind::Blank: kind = "blank"; break;
        case ReaperLineKind::BlockOpen: kind = "block-open"; break;
        case ReaperLineKind::BlockClose: kind = "block-close"; break;
        case ReaperLineKind::Data: kind = "data"; break;
        }
        auto row = Chunk{}.text(i ? ",{\"index\":" : "{\"index\":").number(i)
            .text(",\"kind\":\"").text(kind).text("\",\"parent\":");
        if (node.parent == ReaperStructureNode::noParent) row.text("null");
        else row.number(node.parent);
        row.text(",\"lineRange\":").range(node.line).text(",\"keyRange\":").range(node.key)
            .text(",\"extentRange\":").range(node.extent)
            .text(",\"status\":\"unverified\",\"originalBytesRetained\":true}");
        put(row.bytes());
    }
    put("],\"complete\":true}\n");
    if (output) { output->flush(); require(bool(*output), "Import report flush failed"); }
}
std::string_view errorId(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Canceled: return "import.canceled";
    case ErrorCode::ResourceLimit: return "import.resource_limit";
    case ErrorCode::Io: return "import.io_error";
    case ErrorCode::InvalidParameter: return "import.invalid_request";
    default: return "import.invalid_structure";
    }
}
}
int run(const std::vector<std::string> &args) {
    try {
        require(args.size() >= 3 && args[1] == "--rpp" && (args.size()-3)%2 == 0,
                "Expected selected RPP and trusted limits", ErrorCode::InvalidParameter);
        ReaperStructureLimits limits;
        std::size_t memory = 64*1024*1024, reportBytes = 64*1024*1024;
        for (std::size_t i = 3; i < args.size(); i += 2) {
            const auto value = unsignedOption(args[i+1]);
            if (args[i] == "--memory-bytes") memory = value;
            else if (args[i] == "--maximum-input-bytes") limits.maximumInputBytes = value;
            else if (args[i] == "--maximum-report-bytes") reportBytes = value;
            else throw ProjectError(ErrorCode::InvalidParameter, "Unknown trusted limit");
        }
        require(validUtf8(args[2]) && args[2].find('\0') == std::string::npos,
                "Invalid UTF-8 input path", ErrorCode::InvalidParameter);
        std::signal(SIGINT, signalQuit); std::signal(SIGTERM, signalQuit);
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN); // A closed parent pipe becomes a checked I/O refusal.
#endif
        std::stop_source stop;
        std::jthread canceler([&](std::stop_token own) {
            while (!own.stop_requested()) {
                if (quitRequested.load(std::memory_order_relaxed)) { stop.request_stop(); return; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        ResourceLedger resources(memory, "Import inspection worker payload");
        auto reportCredit = resources.reserve(sizeof(Chunk)+4096);
        const auto path = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t *>(args[2].data()), args[2].size()));
        auto doc = [&] {
            auto source = readForeignSnapshot(path,limits.maximumInputBytes,resources,stop.get_token());
            return inspectReaperStructure(source.bytes(),limits,resources,stop.get_token());
        }();
        const auto hash = hashForeignSnapshot(doc.source(), resources, stop.get_token());
        const std::string_view digest(hash.data(), hash.size());
        // Full bounded sizing pass before stdout: limit refusal publishes no
        // partial report. Parent must still reject interrupted or failed output.
        report(doc, digest, reportBytes, nullptr, stop);
        report(doc, digest, reportBytes, &std::cout, stop);
        return 0;
    } catch (const ProjectError &e) {
        std::cerr << "{\"protocol\":\"sc-import-inspection-v1\",\"complete\":false,\"messageId\":\""
                  << errorId(e.code()) << "\"}\n";
        return e.code() == ErrorCode::Canceled ? 3 : 1;
    } catch (const std::exception &) {
        std::cerr << "{\"protocol\":\"sc-import-inspection-v1\",\"complete\":false,"
                     "\"messageId\":\"import.worker_failure\"}\n";
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                                            nullptr, 0, nullptr, nullptr);
        if (size <= 0) return 1;
        std::string value(static_cast<std::size_t>(size), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), size,
                                 nullptr, nullptr)) return 1;
        value.pop_back(); args.push_back(std::move(value));
    }
    return run(args);
}
#else
int main(int argc, char **argv) { return run({argv, argv+argc}); }
#endif
