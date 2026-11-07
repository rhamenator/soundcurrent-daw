// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "restoration_audit.hpp"
namespace soundcurrent::daw {
struct SavedRestorationDispatchContext {
    SavedRestorationSourceContext source;
    std::uint64_t preparationJob=0,request=0,job=0;
    bool operator==(const SavedRestorationDispatchContext &) const=default;
};
// Borrowed trusted control-owner view, never populated from project/protocol
// metadata. Caller serializes owner/root/I/O changes through bind and verify.
// An empty owner's epoch/revision may be zero; no accepted Session is invented.
struct SavedRestorationOwnerView {
    const std::filesystem::path &root;
    SavedRestorationDispatchContext context;
    bool acceptedProject=false,closing=false,ioActive=false,publicationAmbiguous=false;
};
// Volatile dispatch metadata only. No native owners, process launch, mutation,
// publication permit or adoption authority. An exited stage DTO cannot mint it:
// bind requires the private original source ticket and actual checked report.
class SavedRestorationDispatchTicket {
  public:
    static SavedRestorationDispatchTicket bind(const SavedRestorationOwnerView &,RestorationAudit &,
        const SavedRestorationSourceTicket &,const WaveCheckReport &,std::size_t actualCheckerPid,
        Id freshOperation,ResourceLedger,std::stop_token = {});
    SavedRestorationDispatchTicket(SavedRestorationDispatchTicket &&) noexcept=default;
    SavedRestorationDispatchTicket(const SavedRestorationDispatchTicket &)=delete;
    const Id &operation() const {return operation_;}
    void verify(const SavedRestorationOwnerView &,RestorationAudit &,const SavedRestorationSourceTicket &,
        const WaveCheckReport &,std::size_t actualCheckerPid,ResourceLedger,std::stop_token = {}) const;
  private:
    SavedRestorationDispatchTicket(ResourceLease,OwnedInspectionProtocol,const SavedRestorationOwnerView &,
        Id history,Id freshOperation,std::size_t);
    ResourceLease lease_; // Root and proof storage retire before dispatch credit.
    OwnedInspectionProtocol originalProof_;
    std::filesystem::path root_;
    SavedRestorationDispatchContext context_;
    Id history_,operation_;
    std::size_t checkerPid_=0;
};
} // namespace soundcurrent::daw
