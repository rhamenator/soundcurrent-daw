// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/resource_ledger.hpp>
#include <algorithm>
#include <mutex>
#include <utility>

namespace soundcurrent::daw {
struct ResourceLedgerState {
    mutable std::mutex mutex;
    ResourceUsage usage;
};
namespace {
void positive(std::size_t bytes) {
    if (!bytes)
        throw ProjectError(ErrorCode::InvalidParameter, "Resource limit must be positive");
}
std::size_t admittedTotal(const ResourceUsage &usage, std::size_t retained, std::size_t bytes) {
    PayloadCharge charge("Retained project snapshots", usage.limitBytes);
    charge.add(retained);
    charge.add(bytes);
    return charge.bytes();
}
constexpr std::size_t storageAllowance = 256;
struct OwnedSession {
    ResourceLease lease; // Released after Session destruction, including its allocations.
    Session value;
};
} // namespace
ResourceLedger::ResourceLedger(std::size_t bytes)
    : state_(std::make_shared<ResourceLedgerState>()) {
    positive(bytes);
    state_->usage.limitBytes = bytes;
}
ResourceUsage ResourceLedger::usage() const {
    std::lock_guard lock(state_->mutex);
    return state_->usage;
}
bool ResourceLedger::owns(const ResourceLease &lease) const noexcept {
    return lease.state_ == state_;
}
void ResourceLedger::configure(std::size_t bytes) const {
    positive(bytes);
    std::lock_guard lock(state_->mutex);
    if (bytes < state_->usage.reservedBytes)
        throw ResourceLimitError("Retained project snapshots", state_->usage.reservedBytes, bytes);
    state_->usage.limitBytes = bytes;
}
ResourceLease ResourceLedger::reserve(std::size_t bytes) const {
    if (!bytes)
        return {};
    std::lock_guard lock(state_->mutex);
    auto &usage = state_->usage;
    const auto total = admittedTotal(usage, usage.reservedBytes, bytes);
    if (usage.owners == std::numeric_limits<std::size_t>::max())
        throw ResourceLimitError("Snapshot owner count", usage.owners, usage.owners, true);
    usage.reservedBytes = total;
    usage.peakBytes = std::max(usage.peakBytes, total);
    ++usage.owners;
    return ResourceLease(state_, bytes);
}
ResourceLease::ResourceLease(std::shared_ptr<ResourceLedgerState> state, std::size_t bytes)
    : state_(std::move(state)), bytes_(bytes) {}
ResourceLease::~ResourceLease() {
    release();
}
ResourceLease::ResourceLease(ResourceLease &&other) noexcept
    : state_(std::move(other.state_)), bytes_(std::exchange(other.bytes_, 0)) {}
ResourceLease &ResourceLease::operator=(ResourceLease &&other) noexcept {
    if (this != &other) {
        release();
        state_ = std::move(other.state_);
        bytes_ = std::exchange(other.bytes_, 0);
    }
    return *this;
}
void ResourceLease::release() noexcept {
    if (!state_)
        return;
    {
        std::lock_guard lock(state_->mutex);
        state_->usage.reservedBytes -= bytes_;
        --state_->usage.owners;
    }
    state_.reset();
    bytes_ = 0;
}
void ResourceLease::resize(std::size_t bytes) {
    if (!state_) {
        if (bytes)
            throw ProjectError(ErrorCode::InvalidState, "Cannot grow an empty resource lease");
        return;
    }
    if (!bytes) {
        release();
        return;
    }
    std::lock_guard lock(state_->mutex);
    auto &usage = state_->usage;
    const auto total = admittedTotal(usage, usage.reservedBytes - bytes_, bytes);
    usage.reservedBytes = total;
    usage.peakBytes = std::max(usage.peakBytes, total);
    bytes_ = bytes;
}
std::size_t SessionSnapshots::charge(const Session &value) const {
    PayloadCharge bytes("Session snapshot", std::numeric_limits<std::size_t>::max());
    bytes.add(sessionPayloadBytes(value, state_));
    bytes.add(storageAllowance);
    return bytes.bytes();
}
std::shared_ptr<const Session> SessionSnapshots::copy(const Session &value) const {
    auto lease = ledger_.reserve(charge(value)); // Admission before copying the owned payload.
    auto owner = std::make_shared<OwnedSession>(std::move(lease), value);
    owner->lease.resize(charge(owner->value)); // Account for the copy's actual capacities.
    return {owner, &owner->value};
}
ResourceLease SessionSnapshots::reserveLoad() const {
    PayloadCharge bytes("Session load reservation", std::numeric_limits<std::size_t>::max());
    bytes.add(state_.memoryBudgetBytes);
    bytes.add(storageAllowance);
    return ledger_.reserve(bytes.bytes());
}
std::shared_ptr<const Session> SessionSnapshots::adopt(Session &&value, ResourceLease lease) const {
    if (!ledger_.owns(lease))
        throw ProjectError(ErrorCode::InvalidState,
                           "Session load reservation belongs to another ledger");
    validate(value, state_);
    const auto bytes = charge(value);
    if (lease.bytes() < bytes)
        throw ResourceLimitError("Session load reservation", bytes, lease.bytes());
    lease.resize(bytes);
    auto owner = std::make_shared<OwnedSession>(std::move(lease), std::move(value));
    return {owner, &owner->value};
}
} // namespace soundcurrent::daw
