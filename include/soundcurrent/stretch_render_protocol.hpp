// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "stretch.hpp"
#include "resource_ledger.hpp"
#include <filesystem>
#include <stop_token>
namespace soundcurrent::daw {
inline constexpr std::string_view stretchRenderProtocol="sc-stretch-render-v3";
inline constexpr std::size_t stretchProtocolMaximum=16384;
struct StretchRenderPolicy {
    std::uint64_t memoryBytes=256ULL*1024*1024;
    std::uint64_t maximumInputFrames=1000000000;
    std::uint64_t maximumOutputBytes=8ULL*1024*1024*1024;
    std::uint64_t maximumSourceBytes=8ULL*1024*1024*1024;
    std::uint64_t deadlineMilliseconds=60000;
};
class OwnedStretchProtocol {
 public:
    OwnedStretchProtocol(ResourceLease lease,std::string value):lease_(std::move(lease)),bytes_(std::move(value)) {}
    OwnedStretchProtocol(OwnedStretchProtocol &&) noexcept=default;
    std::string_view bytes() const noexcept {return bytes_;}
 private:
    ResourceLease lease_;std::string bytes_;
};
// Serialized control/I/O only. Caller admits the whole child process ceiling
// before spawn and holds it through reaping; these grants cover codec payload.
OwnedStretchProtocol encodeStretchRenderKey(const Asset &,const ClipStretchAnchor &,ResourceLedger);
std::string stretchRenderKey(const Asset &,const ClipStretchAnchor &,ResourceLedger);
OwnedStretchProtocol encodeStretchRenderRequest(const ClipStretchPlan &,const Id &,const StretchRenderPolicy &,ResourceLedger);
void verifyStretchRenderReady(std::string_view,const ClipStretchPlan &,const Id &,ResourceLedger);
class VerifiedClipStretch {
 public:
    VerifiedClipStretch(VerifiedClipStretch &&) noexcept=default;
    const ApplyClipStretch &edit() const noexcept {return edit_;}
    double peak() const noexcept {return peak_;}
    bool ownedBy(ResourceLedger ledger) const noexcept {return ledger.owns(lease_);}
 private:
    friend VerifiedClipStretch verifyOwnedClipStretch(const std::filesystem::path &,const ClipStretchPlan &,const Id &,const StretchRenderPolicy &,ResourceLedger,std::stop_token);
    VerifiedClipStretch(ResourceLease lease,ApplyClipStretch edit,double peak):lease_(std::move(lease)),edit_(std::move(edit)),peak_(peak) {}
    ResourceLease lease_;ApplyClipStretch edit_;double peak_;
};
// After child reaping, inspect the owned marker even after ambiguous termination.
// Independently hash the raw input and decode/hash all derived float RF64 samples
// before producing a transaction. No directory creation, cleanup or adoption here.
// OS I/O cancellation/deadlines are cooperative; this is not a privilege sandbox.
VerifiedClipStretch verifyOwnedClipStretch(const std::filesystem::path &,const ClipStretchPlan &,const Id &,const StretchRenderPolicy &,ResourceLedger,std::stop_token={});
} // namespace soundcurrent::daw
