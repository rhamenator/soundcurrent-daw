// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_store.hpp"
#include <optional>

namespace soundcurrent::daw {
enum class ExportTail { ExactRange, UntilSilent };
enum class ExportBoundary { Prepared, BlockWritten, BeforeFlush, BeforePublish, DirectoryFlush };
struct ExportDestination {
    std::filesystem::path path;
    bool exists = false;
    std::uintmax_t bytes = 0;
    std::string sha256;
};
// Worker-side safe path/content inspection for a user confirmation receipt.
ExportDestination inspectExportDestination(const std::filesystem::path &projectRoot,
                                           const std::filesystem::path &destination,
                                           const std::function<void()> &beforeRead = {});
struct ExportSpec {
    explicit ExportSpec(const Id &track) : trackId(track) {}
    Id trackId;
    Frame startFrame = 0, endFrame = 0; // Half-open timeline selection, not source offsets.
    ExportTail tail = ExportTail::ExactRange;
    Frame maximumTailFrames = 480000;
    std::uint32_t silentWindowFrames = 4800;
    double silenceAmplitude = 1e-6;
    std::uint32_t blockFrames = 512;
    std::size_t memoryBudgetBytes = 128 * 1024 * 1024;
    // Explicit resource admission, includes preroll and maximum tail. Caller may raise it.
    Frame maximumProcessFrames = 48000LL * 60 * 60 * 24;
    bool forceRf64 = false;
    // No token means exclusive no-overwrite. A token approves replacement of these bytes.
    // Hash/recheck is for the owned-filesystem contract, not hostile-writer atomic CAS.
    std::optional<std::string> replaceSha256;
};
struct ExportOptions {
    // All callbacks run on the calling I/O owner, never on audio or the GUI.
    std::function<bool()> canceled;
    std::function<void(Frame written, Frame maximum)> progress;
    std::function<void(ExportBoundary, Frame written)> boundary;
};
struct ExportResult {
    std::filesystem::path destination;
    Frame startFrame = 0, endFrame = 0, processingStartFrame = 0;
    Frame frames = 0, tailFrames = 0;
    std::uint32_t sampleRate = 0;
    ChannelLayout layout;
    double peak = 0;
    std::uint64_t overFullScaleSamples = 0;
    bool tailTruncated = false, rf64 = false, replaced = false;
    std::string sampleSha256, fileSha256;
    Durability durability = Durability::FileFlushed;
    // Publication has happened: a directory flush/alias-cleanup failure is reported
    // as a complete published file with weaker durability, not a failed partial export.
    std::string publicationWarning;
};
// Synchronous worker-side job, framework/device independent. Immutable model is copied
// by its caller before dispatch. Owns a private reader/EQ/transaction, never live state.
// Destination parent must exist and be plain. Inside a project only exports/ is allowed.
// Nonfinite source/processor samples fail rather than silently publishing sanitized audio.
ExportResult exportTrackWav(const std::filesystem::path &projectRoot, const Session &,
                            const std::filesystem::path &destination, const ExportSpec &,
                            const ExportOptions & = {});
} // namespace soundcurrent::daw
