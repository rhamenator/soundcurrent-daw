// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import_inspection_report.hpp"
#include "project_store.hpp"

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
// Off audio/GUI. New-file publication only; existing files/links/directories are
// never replaced. One self-contained v1 container preserves exact source and
// validated inspection protocol. No foreign dependency path is resolved.
InspectionBundleSaveResult saveInspectionBundle(const std::filesystem::path &,
    const ImportInspectionReport &,ResourceLedger,InspectionBundleLimits={},
    const InspectionBundleSaveOptions &options={});
ImportInspectionReport loadInspectionBundle(const std::filesystem::path &,
    ResourceLedger,InspectionBundleLimits={},std::stop_token={});
} // namespace soundcurrent::daw
