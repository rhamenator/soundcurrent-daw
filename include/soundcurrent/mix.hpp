// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "eq.hpp"

namespace soundcurrent::daw {
struct ChannelMix {
    std::uint32_t source = 0, destination = 0;
    double gain = 1;
    bool operator==(const ChannelMix &) const = default;
};
struct TrackMix {
    Id track;
    std::vector<ChannelMix> channels;
    bool operator==(const TrackMix &) const = default;
};
struct MixPlan {
    ChannelLayout output;
    std::vector<TrackMix> tracks;
    bool operator==(const MixPlan &) const = default;
};
// Explicit same-layout identity routes. Never drops or guesses a channel map.
MixPlan identityMix(const Session &, std::span<const Id> tracks, ChannelLayout output);
struct MixConfig {
    std::uint32_t maximumFrames = 2048;
    Frame startFrame = 0;
    std::uint64_t generation = 1;
    std::size_t memoryBudgetBytes = 128 * 1024 * 1024;
    std::size_t maximumRoutingEntries = 65536;
};
// Conservative owned DSP/vector/string payload admission, not allocator/RSS accounting.
std::size_t mixPayloadBytes(const Session &, const MixPlan &, const MixConfig &);
using MixInput = std::span<const float *const>;
struct MixEvent {
    std::size_t track = 0; // Prepared ordinal, never persisted; event carries generation.
    EqEvent event;
};
struct MixReport {
    ProcessStatus status = ProcessStatus::Ok;
    Frame startFrame = 0;
    std::uint32_t frames = 0;
    std::uint64_t eventsApplied = 0, invalidInputSamples = 0, numericFaultSamples = 0;
    double peak = 0;
};
// One audio owner, one serialized parameter producer. Prepare/destroy off RT.
// Inputs follow the prepared track order; output planes must be distinct.
class PreparedMixGraph {
  public:
    PreparedMixGraph(const Session &, MixPlan, MixConfig = {});
    ~PreparedMixGraph();
    PreparedMixGraph(const PreparedMixGraph &) = delete;
    PreparedMixGraph &operator=(const PreparedMixGraph &) = delete;
    MixReport process(std::span<const MixInput>, std::span<float *const>,
                      std::uint32_t frames) noexcept;
    MixReport processSilence(std::span<float *const>, std::uint32_t frames) noexcept;
    void stop() noexcept; // Audio owner only; control must request via its adapter.
    Frame position() const noexcept;
    const MixPlan &plan() const noexcept;
    const MixConfig &config() const noexcept;
    PreparedEq &prepared(std::size_t track); // Control-only immutable mapping lookup.
    MixEvent parameterEvent(const Session &, const ParameterAddress &, Frame at) const;
    MixEvent enableEvent(const Id &track, bool enabled, Frame at) const;
    SubmitStatus submit(const MixEvent &e) noexcept {
        return submit(e.track, e.event);
    }
    SubmitStatus submitImmediate(const MixEvent &e, std::uint64_t revision) noexcept {
        return submitImmediate(e.track, e.event, revision);
    }
    SubmitStatus submit(std::size_t track, const EqEvent &) noexcept;
    SubmitStatus submitImmediate(std::size_t track, const EqEvent &,
                                 std::uint64_t revision) noexcept;
    bool acknowledgement(std::size_t track, ImmediateAcknowledgement &) noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
