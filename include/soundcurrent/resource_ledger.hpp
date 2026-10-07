// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <memory>

namespace soundcurrent::daw {
struct ResourceUsage {
    std::size_t limitBytes = 0, reservedBytes = 0, peakBytes = 0, owners = 0;
    bool operator==(const ResourceUsage &) const = default;
};
struct ResourceLedgerState;
struct ResourceScopeState;
// Off audio only: acquisition, configuration and last-owner destruction take a mutex.
// A lease may outlive its ledger facade. Failed admission leaves usage unchanged.
class ResourceLease {
  public:
    ResourceLease() = default;
    ~ResourceLease();
    ResourceLease(ResourceLease &&) noexcept;
    ResourceLease &operator=(ResourceLease &&) noexcept;
    ResourceLease(const ResourceLease &) = delete;
    ResourceLease &operator=(const ResourceLease &) = delete;
    void resize(std::size_t);
    // Same ledger/scope only. Moves already admitted credit without a new reservation.
    void transferTo(ResourceLease &, std::size_t);
    std::size_t bytes() const noexcept {
        return bytes_;
    }

  private:
    friend class ResourceLedger;
    ResourceLease(std::shared_ptr<ResourceLedgerState>, std::shared_ptr<ResourceScopeState>,
                  std::size_t);
    void release() noexcept;
    std::shared_ptr<ResourceLedgerState> state_;
    std::shared_ptr<ResourceScopeState> scope_;
    std::size_t bytes_ = 0;
};
class ResourceLedger {
  public:
    explicit ResourceLedger(std::size_t limitBytes = 256 * 1024 * 1024,
                            std::string resource = "Retained project snapshots");
    // Parent and child admission/configuration share one mutex and one atomic decision.
    ResourceLedger child(std::size_t limitBytes, std::string resource) const;
    ResourceLease reserve(std::size_t) const;
    ResourceUsage usage() const;
    void configure(std::size_t limitBytes) const;
    void configureWith(const ResourceLedger &child, std::size_t parentBytes,
                       std::size_t childBytes) const;
    bool owns(const ResourceLease &) const noexcept;

  private:
    std::shared_ptr<ResourceLedgerState> state_;
    std::shared_ptr<ResourceScopeState> scope_;
};
// Counts each immutable Session block once across all shared_ptr borrowers.
// Payload charges include the existing conservative validation allowances;
// they do not claim exact allocator/RSS measurements or account for GUI indices.
class SessionSnapshots {
  public:
    explicit SessionSnapshots(ResourceLedger ledger = ResourceLedger{}, StateBudget state = {})
        : ledger_(std::move(ledger)), state_(state) {}
    std::shared_ptr<const Session> copy(const Session &) const;
    // Reserve before decoding an unknown-size project, then move its validated state.
    ResourceLease reserveLoad() const;
    std::shared_ptr<const Session> adopt(Session &&, ResourceLease) const;
    ResourceUsage usage() const {
        return ledger_.usage();
    }
    void configure(std::size_t bytes) const {
        ledger_.configure(bytes);
    }
    ResourceLedger resourceLedger() const {
        return ledger_;
    }

  private:
    std::size_t charge(const Session &) const;
    ResourceLedger ledger_;
    StateBudget state_;
};
} // namespace soundcurrent::daw
