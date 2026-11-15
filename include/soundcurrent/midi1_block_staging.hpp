// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "midi1_ingress.hpp"
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

namespace soundcurrent::daw {
struct Midi1BlockStagingReport {
    Midi1BlockReport block;
    std::size_t transferred = 0; // Queue values moved into consumer-owned pending storage.
    std::size_t pending = 0;
};
// One consumer owns staging and its ingress consumer operations. Prepare/retire
// off RT, joining both queue owners before retirement. No reset or silent drops.
class PreparedMidi1BlockStaging {
  public:
    explicit PreparedMidi1BlockStaging(PreparedMidi1Ingress &ingress)
        : ingress_(ingress), identity_(ingress.identity()) {
        pending_.reserve(maxMidi1BlockEvents + 1);
    }
    PreparedMidi1BlockStaging(const PreparedMidi1BlockStaging &) = delete;
    PreparedMidi1BlockStaging &operator=(const PreparedMidi1BlockStaging &) = delete;
    PreparedMidi1BlockStaging(PreparedMidi1BlockStaging &&) = delete;
    PreparedMidi1BlockStaging &operator=(PreparedMidi1BlockStaging &&) = delete;

    Midi1BlockStagingReport tryRender(Midi1BlockWindow window, std::span<std::optional<Midi1BlockEvent>> output) noexcept {
        const auto checked = admitMidi1Block(window, {}, {});
        if (checked.status != Midi1BlockStatus::Ok) return report(checked, 0);
        if (window.generation != identity_.generation)
            return report({Midi1BlockStatus::InvalidEvent}, 0);
        if (!pending_.empty() && pending_.front().frame < window.start)
            return report({Midi1BlockStatus::InvalidEvent}, 0);
        const Frame end = window.start + Frame(window.frames); // Checked by admission above.
        std::size_t transferred = 0;
        // Accepted ingress order means everything after the first future event
        // is future too. Stop there, or after the first excess active event.
        while ((pending_.empty() || pending_.back().frame < end) &&
               pending_.size() < maxMidi1BlockEvents + 1) {
            std::optional<TimedMidi1Event> event;
            if (!ingress_.tryPop(event)) break;
            if (!event) return report({Midi1BlockStatus::InvalidEvent}, transferred);
            pending_.push_back(*event); // Prepared capacity, nothrow value copy.
            ++transferred;
        }
        const auto result = admitMidi1Block(window, pending_, output);
        if (result.status == Midi1BlockStatus::Ok)
            pending_.erase(pending_.begin(), pending_.begin() + std::ptrdiff_t(result.emitted));
        return report(result, transferred);
    }
    // Borrowed view for this consumer only, invalidated by the next mutation or retirement.
    std::span<const TimedMidi1Event> pendingEvents() const & noexcept { return pending_; }
    std::span<const TimedMidi1Event> pendingEvents() const && = delete;
    Midi1IngressIdentity identity() const noexcept { return identity_; }
  private:
    Midi1BlockStagingReport report(Midi1BlockReport block, std::size_t transferred) const noexcept {
        return {block, transferred, pending_.size()};
    }
    PreparedMidi1Ingress &ingress_; // Must outlive this consumer owner.
    const Midi1IngressIdentity identity_;
    std::vector<TimedMidi1Event> pending_;
    static_assert(std::is_nothrow_copy_constructible_v<TimedMidi1Event>);
    static_assert(std::is_nothrow_move_assignable_v<TimedMidi1Event>);
};
}
