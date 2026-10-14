// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "comp_span_protocol.hpp"
namespace soundcurrent::daw {
// Off audio/GUI. Review every regenerated side's original history, including an
// unchanged sibling. No new media or canonical state mutation. Worker calls this
// before readiness and completion; consumers review again before use. Each side
// uses bounded iterative review; containing worker memory/deadline limits apply.
void verifyCompSpanHistory(const std::filesystem::path &,const CompSpanRequest &,
    const CompSpanWorkerPolicy &,ResourceLedger,const std::function<void()> &poll,std::stop_token={});
} // namespace soundcurrent::daw
