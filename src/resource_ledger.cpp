// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/resource_ledger.hpp>
#include <algorithm>
#include <mutex>
#include <utility>

namespace soundcurrent::daw {
struct ResourceScopeState {
    ResourceUsage usage;
    std::string resource;
};
struct ResourceLedgerState {
    mutable std::mutex mutex;
    ResourceUsage usage;
    std::string resource;
};
namespace {
void positive(std::size_t bytes) {
    if (!bytes)
        throw ProjectError(ErrorCode::InvalidParameter, "Resource limit must be positive");
}
std::size_t admittedTotal(const ResourceUsage &usage, std::string_view resource,
                          std::size_t retained, std::size_t bytes) {
    PayloadCharge charge(std::string(resource), usage.limitBytes);
    charge.add(retained);
    charge.add(bytes);
    return charge.bytes();
}
void ownerRoom(const ResourceUsage &usage) {
    if (usage.owners == std::numeric_limits<std::size_t>::max())
        throw ResourceLimitError("Resource owner count", usage.owners, usage.owners, true);
}
void accepted(ResourceUsage &usage, std::size_t total) {
    usage.reservedBytes = total;
    usage.peakBytes = std::max(usage.peakBytes, total);
}
constexpr std::size_t storageAllowance = 256;
struct OwnedSession {
    ResourceLease lease; // Released after Session destruction, including its allocations.
    Session value;
};
} // namespace
ResourceLedger::ResourceLedger(std::size_t bytes, std::string resource)
    : state_(std::make_shared<ResourceLedgerState>()) {
    positive(bytes);
    state_->usage.limitBytes = bytes;
    state_->resource = std::move(resource);
}
ResourceLedger ResourceLedger::child(std::size_t bytes, std::string resource) const {
    positive(bytes);
    if (scope_)
        throw ProjectError(ErrorCode::InvalidState,
                           "Create resource scopes from the parent ledger");
    auto result = *this;
    result.scope_ = std::make_shared<ResourceScopeState>();
    result.scope_->usage.limitBytes = bytes;
    result.scope_->resource = std::move(resource);
    return result;
}
ResourceUsage ResourceLedger::usage() const {
    std::lock_guard lock(state_->mutex);
    return scope_ ? scope_->usage : state_->usage;
}
bool ResourceLedger::owns(const ResourceLease &lease) const noexcept {
    return lease.state_ == state_ && lease.scope_ == scope_;
}
void ResourceLedger::configure(std::size_t bytes) const {
    positive(bytes);
    std::lock_guard lock(state_->mutex);
    auto &usage = scope_ ? scope_->usage : state_->usage;
    const auto &resource = scope_ ? scope_->resource : state_->resource;
    if (bytes < usage.reservedBytes)
        throw ResourceLimitError(resource, usage.reservedBytes, bytes);
    usage.limitBytes = bytes;
}
void ResourceLedger::configureWith(const ResourceLedger &child, std::size_t parentBytes,
                                   std::size_t childBytes) const {
    positive(parentBytes);
    positive(childBytes);
    if (scope_ || state_ != child.state_ || !child.scope_)
        throw ProjectError(ErrorCode::InvalidState,
                           "Joint configuration needs a parent and its child");
    std::lock_guard lock(state_->mutex);
    if (parentBytes < state_->usage.reservedBytes)
        throw ResourceLimitError(state_->resource, state_->usage.reservedBytes, parentBytes);
    if (childBytes < child.scope_->usage.reservedBytes)
        throw ResourceLimitError(child.scope_->resource, child.scope_->usage.reservedBytes,
                                 childBytes);
    state_->usage.limitBytes = parentBytes;
    child.scope_->usage.limitBytes = childBytes;
}
ResourceLease ResourceLedger::reserve(std::size_t bytes) const {
    if (!bytes)
        return {};
    std::lock_guard lock(state_->mutex);
    auto &usage = state_->usage;
    const auto total = admittedTotal(usage, state_->resource, usage.reservedBytes, bytes);
    const auto scoped =
        scope_ ? admittedTotal(scope_->usage, scope_->resource, scope_->usage.reservedBytes, bytes)
               : 0;
    ownerRoom(usage);
    if (scope_)
        ownerRoom(scope_->usage);
    accepted(usage, total);
    ++usage.owners;
    if (scope_) {
        accepted(scope_->usage, scoped);
        ++scope_->usage.owners;
    }
    return ResourceLease(state_, scope_, bytes);
}
ResourceLease::ResourceLease(std::shared_ptr<ResourceLedgerState> state,
                             std::shared_ptr<ResourceScopeState> scope, std::size_t bytes)
    : state_(std::move(state)), scope_(std::move(scope)), bytes_(bytes) {}
ResourceLease::~ResourceLease() {
    release();
}
ResourceLease::ResourceLease(ResourceLease &&other) noexcept
    : state_(std::move(other.state_)), scope_(std::move(other.scope_)),
      bytes_(std::exchange(other.bytes_, 0)) {}
ResourceLease &ResourceLease::operator=(ResourceLease &&other) noexcept {
    if (this != &other) {
        release();
        state_ = std::move(other.state_);
        scope_ = std::move(other.scope_);
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
        if (scope_) {
            scope_->usage.reservedBytes -= bytes_;
            --scope_->usage.owners;
        }
    }
    state_.reset();
    scope_.reset();
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
    const auto total =
        admittedTotal(state_->usage, state_->resource, state_->usage.reservedBytes - bytes_, bytes);
    const auto scoped = scope_ ? admittedTotal(scope_->usage, scope_->resource,
                                               scope_->usage.reservedBytes - bytes_, bytes)
                               : 0;
    accepted(state_->usage, total);
    if (scope_)
        accepted(scope_->usage, scoped);
    bytes_ = bytes;
}
void ResourceLease::transferTo(ResourceLease &destination, std::size_t bytes) {
    if (!bytes)
        return;
    if (&destination == this || !state_ || bytes > bytes_ ||
        (destination.state_ && (destination.state_ != state_ || destination.scope_ != scope_)))
        throw ProjectError(ErrorCode::InvalidState,
                           "Resource credit transfer needs matching leases");
    // Shared pointers are copied without allocating; keep counters alive while the source empties.
    const auto state = state_;
    const auto scope = scope_;
    std::lock_guard lock(state->mutex);
    const bool creates = !destination.state_, retires = bytes == bytes_;
    if (creates && !retires) {
        ownerRoom(state->usage);
        if (scope)
            ownerRoom(scope->usage);
    }
    if (creates) {
        destination.state_ = state;
        destination.scope_ = scope;
        ++state->usage.owners;
        if (scope)
            ++scope->usage.owners;
    }
    destination.bytes_ += bytes; // Sum cannot exceed the already admitted total.
    bytes_ -= bytes;
    if (retires) {
        --state->usage.owners;
        if (scope)
            --scope->usage.owners;
        state_.reset();
        scope_.reset();
    }
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
