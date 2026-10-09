// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import_inspection_report.hpp"
#include "project_store.hpp"
#include "approved_media.hpp"

namespace soundcurrent::daw {
struct InspectionBundleLimits {
    std::size_t sourceBytes=16*1024*1024, protocolBytes=64*1024*1024;
};
struct InspectionBundleSaveOptions {
    std::stop_token stop;
    std::function<void(std::size_t)> afterWrite; // I/O-thread qualification boundary.
    std::function<void()> beforePublish;
};
struct InspectionBundleSaveResult {
    std::size_t bytes=0;
    Durability durability=Durability::FileFlushed;
};
struct InspectionBundleFingerprint {
    std::size_t bytes = 0;
    std::array<char,64> sha256{};
};
// Off audio/GUI. New-file publication only; existing files/links/directories are
// never replaced. One self-contained v1 container preserves exact source and
// validated inspection protocol. No foreign dependency path is resolved.
InspectionBundleSaveResult saveInspectionBundle(const std::filesystem::path &,
    const ImportInspectionReport &,ResourceLedger,InspectionBundleLimits={},
    const InspectionBundleSaveOptions &options={});
ImportInspectionReport loadInspectionBundle(const std::filesystem::path &,
    ResourceLedger,InspectionBundleLimits={},std::stop_token={},
    InspectionBundleFingerprint * = nullptr);
// Pinned project-directory capability variant: never reopens a pathname.
// beforeRead polls on the I/O owner before 64KiB reads and decode boundaries.
ImportInspectionReport loadInspectionBundle(ApprovedMediaFile &,ResourceLedger,
    InspectionBundleLimits={},std::stop_token={},InspectionBundleFingerprint * = nullptr,
    const std::function<void()> &beforeRead = {});
// Original bounded receipt bytes, new-file publication only. No generic overwrite.
InspectionBundleSaveResult saveNewProjectEvidenceFile(const std::filesystem::path &,
    std::string_view,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
