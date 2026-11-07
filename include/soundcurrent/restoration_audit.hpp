// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "restoration_record.hpp"
#include "project_store.hpp"
#include "wave_report.hpp"
namespace soundcurrent::daw {
enum class RestorationMediaObservation { Missing, Verified };
enum class RestorationReceiptObservation { Missing, Partial, Final };
struct RestorationAuditState;
class SavedRestorationSourceTicket;
class SavedRestorationStage;
struct SavedRestorationSourceContext;
// Read-only I/O-worker snapshot, never publication/resume/rollback authority.
// The manifest is passively admitted; unrelated media need not exist. Strict
// ProjectStore::load and fresh controller ownership remain separate gates.
class RestorationAudit {
  public:
    RestorationAudit(RestorationAudit &&) noexcept;
    ~RestorationAudit();
    RestorationAudit(const RestorationAudit &)=delete;
    const Session &manifest() const;
    std::string_view manifestBytes() const;
    std::array<char,64> manifestSha256() const;
    const AssetRestorationRecordData &intent() const;
    RestorationMediaObservation canonical() const;
    RestorationMediaObservation staged() const;
    RestorationReceiptObservation receipt() const;
    bool ownedBy(ResourceLedger) const;
    // Checks pinned bytes AND current relative-name observations again. Not an
    // atomic namespace snapshot against hostile same-user directory writers.
    void reverify(std::stop_token = {});
    // Read-only I/O proof for one live, unattempted restoration stage that
    // captured this exact parent before/after its exclusive staging work.
    // Keeps ordinary reverify strict and preserves every original pinned/named
    // byte. No publication, replay, model adoption or current-controller grant.
    void reverifyAfterOwnedStaging(const PreparedAssetRestoration &,std::stop_token = {});
  private:
    friend class SavedRestorationSourceTicket;
    friend class SavedRestorationStage;
    friend void verifySavedRestorationSourceReport(const SavedRestorationSourceTicket &,RestorationAudit &,
        const WaveCheckReport &,SavedRestorationSourceContext,std::size_t,ResourceLedger,std::stop_token);
    friend RestorationAudit inspectAssetRestoration(const std::filesystem::path &,const Id &,const Asset &,
        const Id &,std::uint64_t,ResourceLedger,ProjectBudget,std::stop_token);
    friend RestorationAudit inspectSavedAssetRestoration(const std::filesystem::path &,const Id &,const Id &,
        const Id &,std::uint64_t,ResourceLedger,ProjectBudget,std::stop_token);
    explicit RestorationAudit(std::unique_ptr<RestorationAuditState>);
    RestorationAuditState &state() const;
    void reverifyFiles(std::stop_token);
    std::unique_ptr<RestorationAuditState> state_;
};
// Explicit trusted root/project/asset/operation and size budgets. Historical
// intent/receipt tokens are compared as evidence, never used as current tokens.
RestorationAudit inspectAssetRestoration(const std::filesystem::path &,const Id &project,const Asset &,
    const Id &operation,std::uint64_t maximumMediaBytes,ResourceLedger,ProjectBudget = {},std::stop_token = {});
// Pre-open observation only: resolve the selected asset descriptor from this
// pinned, strictly decoded saved manifest. No accepted Session is required, and
// unrelated missing media do not prevent inspection. The descriptor is passive
// evidence, NOT checked-audio, model-adoption or current-owner authority.
RestorationAudit inspectSavedAssetRestoration(const std::filesystem::path &,const Id &project,const Id &asset,
    const Id &operation,std::uint64_t maximumMediaBytes,ResourceLedger,ProjectBudget = {},std::stop_token = {});
// Fresh trusted owner context, never loaded from a saved record. An empty
// controller's epoch/revision zero remain valid; request/job must be nonzero.
struct SavedRestorationSourceContext {
    std::uint64_t projectEpoch=0,modelRevision=0,request=0,job=0;
    bool operator==(const SavedRestorationSourceContext &) const=default;
};
// I/O-only metadata binding captured BEFORE the selected checker dispatch.
// No native handles, passive Session, source reopen or publication authority.
inline constexpr std::size_t savedSourceControlMaximumBytes=8192;
class SavedRestorationSourceTicket {
  public:
    SavedRestorationSourceTicket(RestorationAudit &,std::string_view selectedRelative,
        SavedRestorationSourceContext,ResourceLedger,std::stop_token = {});
    // Volatile trusted-parent control metadata only; never persist as a source,
    // controller or publication capability. Child owns a fresh native audit and
    // must match this ORIGINAL private fingerprint, not recapture a newer one.
    OwnedInspectionProtocol copyControlProtocol(RestorationAudit &,const WaveCheckReport &,
        SavedRestorationSourceContext,std::size_t observedPid,ResourceLedger,std::stop_token = {}) const;
    static SavedRestorationSourceTicket fromControlProtocol(std::string_view,RestorationAudit &,const WaveCheckReport &,
        SavedRestorationSourceContext current,std::size_t observedPid,ResourceLedger,std::stop_token = {});
    SavedRestorationSourceTicket(SavedRestorationSourceTicket &&) noexcept=default;
    SavedRestorationSourceTicket(const SavedRestorationSourceTicket &)=delete;
  private:
    friend void verifySavedRestorationSourceReport(const SavedRestorationSourceTicket &,RestorationAudit &,
        const WaveCheckReport &,SavedRestorationSourceContext,std::size_t,ResourceLedger,std::stop_token);
    ResourceLease lease_; // Selected name retires before its admitted metadata credit.
    SavedRestorationSourceContext context_;
    ApprovedDirectoryStamp root_;
    std::array<std::array<char,64>,6> hashes_{};
    std::array<bool,6> present_{};
    std::string selectedRelative_;
    std::array<char,64> sourceSha256_{};
    std::uint64_t sourceBytes_=0,frames_=0;
    std::uint32_t rate_=0,channels_=0;
};
// Context and observed PID must come from the CURRENT serialized I/O owner and
// its actual completed checker job, never from a GUI DTO or protocol fields.
// Re-verifies the current native audit and exact saved/record/media fingerprint.
void verifySavedRestorationSourceReport(const SavedRestorationSourceTicket &,RestorationAudit &,
    const WaveCheckReport &,SavedRestorationSourceContext,std::size_t observedPid,ResourceLedger,std::stop_token = {});
// Exact admitted checker bytes for a later isolated-worker handoff. This copy
// grants no source/destination access, staging, model adoption or publication.
OwnedInspectionProtocol copySavedRestorationSourceProtocol(const SavedRestorationSourceTicket &,RestorationAudit &,
    const WaveCheckReport &,SavedRestorationSourceContext,std::size_t observedPid,ResourceLedger,std::stop_token = {});
// I/O-only qualification of a freshly approved explicit source handle against
// the current saved owner and completed checker. No pathname reopen or decode;
// the actual checker must already have decoded finite audio. Re-hashes the
// entire bounded pinned source and re-verifies source and saved evidence after
// hashing. The serialized caller retains the native handle and must revalidate
// before later use. This grants no staging/publication/model-adoption authority.
void verifySavedRestorationSourceFile(const SavedRestorationSourceTicket &,RestorationAudit &,
    const WaveCheckReport &,SavedRestorationSourceContext,std::size_t observedPid,ApprovedMediaFile &,
    ResourceLedger,std::stop_token = {},const std::function<void()> &beforeChunk = {});
} // namespace soundcurrent::daw
