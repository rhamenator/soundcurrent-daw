// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mix_reader.hpp"
namespace soundcurrent::daw {
struct WasapiOutputConfig {
    std::uint32_t nativeChannels = 0;
    std::vector<std::uint32_t> channels; // Graph channel -> native channel; unique, explicit.
    std::optional<ResourceLedger> resources;
};
// Prepared output interleaving. Graph position is a project frame; callers must
// keep it distinct from both SDK queue position and physical playback clocks.
// One audio owner, no allocation/wait/lock/disk access in process.
class PreparedWasapiOutput {
  public:
    PreparedWasapiOutput(MixPlaybackRun &, WasapiOutputConfig);
    MixPlaybackReport process(std::span<float> interleaved, std::uint32_t frames) noexcept;
  private:
    ResourceLease lease_;
    MixPlaybackRun &run_;
    WasapiOutputConfig config_;
    std::uint32_t maximumFrames_, channels_;
    std::vector<float> samples_;
    std::array<float *, 256> output_{};
};
} // namespace soundcurrent::daw
