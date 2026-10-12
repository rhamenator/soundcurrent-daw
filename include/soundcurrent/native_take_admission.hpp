// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_original_recording.hpp"
#include "clip_timing.hpp"
namespace soundcurrent::daw {
struct NativeTakeAdmissionOptions {
    ResourceLedger resources{16*1024*1024,"Native take admission"};
    std::uint64_t maximumAudioBytes=1024ULL*1024*1024;
    std::function<void()> beforeRead;
};
class VerifiedNativeTake {
  public:
    VerifiedNativeTake(VerifiedNativeTake &&) noexcept=default;
    VerifiedNativeTake(const VerifiedNativeTake &)=delete;
    const InsertNativeTake &edit() const {return edit_;}
    const Id &project() const {return project_;}
    const std::filesystem::path &root() const {return root_;}
    bool ownedBy(const ResourceLedger &ledger) const {return ledger.owns(lease_);}
  private:
    friend VerifiedNativeTake verifyNativeTakeForAdmission(const std::filesystem::path &,const std::filesystem::path &,
        const NativeOriginalRecordingSpec &,const Id &,Frame,std::uint32_t,const Id &,const NativeTakeAdmissionOptions &);
    VerifiedNativeTake(ResourceLease lease,std::filesystem::path root,Id project,InsertNativeTake edit)
        :lease_(std::move(lease)),root_(std::move(root)),project_(std::move(project)),edit_(std::move(edit)){}
    ResourceLease lease_;std::filesystem::path root_;Id project_;InsertNativeTake edit_;
};
// Inactive serialized I/O only. Pins/validates the original and journals, retains
// portable evidence on the asset and gives the clip an explicit user placement.
// No copying, resampling, monitoring EQ printing or native-to-project clock inference.
VerifiedNativeTake verifyNativeTakeForAdmission(const std::filesystem::path &projectRoot,const std::filesystem::path &job,
    const NativeOriginalRecordingSpec &expected,const Id &targetTrack,Frame startFrame,
    std::uint32_t projectRate,const Id &clipId,const NativeTakeAdmissionOptions & = {});
}
