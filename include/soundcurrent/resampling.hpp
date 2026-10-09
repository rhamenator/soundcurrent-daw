// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "clip_timing.hpp"
#include "resource_ledger.hpp"
#include <span>
#include <optional>
#include <string_view>

namespace soundcurrent::daw {
inline constexpr std::string_view resamplingAlgorithmId="soundcurrent.src-best-aligned-v1";
struct ResamplingConfig {
    std::uint32_t sourceRate=48000,projectRate=48000,channels=1;
    std::uint32_t maximumInputFrames=4096,maximumOutputFrames=4096;
    std::size_t memoryBudgetBytes=256*1024*1024;
    std::optional<ResourceLedger> resources;
};
struct ResamplingStatistics {
    Frame acceptedInputFrames=0,consumedInputFrames=0,producedOutputFrames=0;
    bool wantsInput=true,drained=false,failed=false;
};
std::size_t resamplingPayloadBytes(const ResamplingConfig &);
// Conservative pinned filter look-behind/look-ahead in source frames, excluding
// caller input batching. Equal-rate copy needs no context. This is not a native
// device latency measurement or a promise that live inputs arrive in advance.
std::uint32_t resamplingSourceContextFrames(const ResamplingConfig &);
// Serialized worker/read-ahead owner. Construction, retirement and resource
// leases are off RT. There is no file, GUI, backend or Session access here.
// Float input/output preserves headroom. Fixed best-sinc converter; no implicit
// rate automation, independent pitch/stretch or callback-safety promise.
class PreparedResampler {
  public:
    explicit PreparedResampler(ResamplingConfig);
    ~PreparedResampler();
    PreparedResampler(const PreparedResampler &)=delete;
    PreparedResampler &operator=(const PreparedResampler &)=delete;
    // Copies whole interleaved frames into the admitted bounded input buffer.
    // Only when wantsInput; end is terminal and may accompany empty input.
    void push(std::span<const float>,bool end=false);
    // Output domain is [0,ceil(accepted*projectRate/sourceRate)). Bounded virtual
    // zero extension supplies the right filter context; it is never counted as
    // owned input or emitted beyond this domain. Call until wantsInput/drained.
    // A processing failure latches failed; retire rather than retry that state.
    std::uint32_t pull(std::span<float>);
    ResamplingStatistics statistics() const noexcept;
  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw
