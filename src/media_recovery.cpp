// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/media_recovery.hpp>
#include <algorithm>
namespace soundcurrent::daw {
namespace {
void check(bool good,const char *text,ErrorCode code=ErrorCode::MediaMismatch) {if (!good) throw ProjectError(code,text);}
std::unique_ptr<MediaProvenance> record(const ApprovedMediaRoot &root,const std::string &name,
    ResourceLedger ledger,std::stop_token stop) {
    auto bank=ledger.reserve(mediaReceiptMaximumBytes+1024);
    try {
        auto file=root.open(name,mediaReceiptMaximumBytes,stop);
        std::string bytes(static_cast<std::size_t>(file.size()),'\0');file.readAt(0,bytes,stop);file.verifyUnchanged();
        auto result=std::make_unique<MediaProvenance>(decodeMediaProvenance(bytes,ledger,stop));file.verifyUnchanged();return result;
    } catch (const ProjectError &e) {if (e.code()==ErrorCode::MissingMedia) return {};throw;}
}
bool audioEqual(const WaveValidation &a,const WaveValidation &b) {
    return a.sourceBytes==b.sourceBytes && a.frames==b.frames && a.decodedFrames==b.decodedFrames &&
        a.rate==b.rate && a.channels==b.channels && a.bitsPerSample==b.bitsPerSample && a.channelMask==b.channelMask &&
        a.encoding==b.encoding && a.bigEndian==b.bigEndian && a.extensible==b.extensible && a.peak==b.peak && a.sourceSha256==b.sourceSha256;
}
}
MediaRecovery recoverStagedMedia(const std::filesystem::path &parent,const Id &operation,
    ResourceLedger ledger,std::uint64_t maximum,std::stop_token stop) {
    check(maximum>0,"Invalid recovery media policy",ErrorCode::InvalidParameter);
    ApprovedMediaRoot root(parent,ledger,{2});const auto prefix=operation.str()+"/";
    auto intent=record(root,prefix+"intent.json",ledger,stop);auto receipt=record(root,prefix+"receipt.json",ledger,stop);
    if (!intent && !receipt) return {};
    check(bool(intent),"Committed receipt has no original intent",ErrorCode::InvalidState);
    check(intent->data().operation==operation && intent->data().phase==MediaReceiptPhase::Planned,
        "Wrong planned staging identity",ErrorCode::InvalidState);
    if (!receipt) return {MediaRecoveryPhase::Planned,std::move(intent)};
    check(receipt->data().operation==operation && receipt->data().phase==MediaReceiptPhase::Verified,
        "Wrong committed staging identity",ErrorCode::InvalidState);
    {
        auto planned=encodeMediaProvenance(*intent,MediaReceiptPhase::Planned,ledger,stop);
        auto original=encodeMediaProvenance(*receipt,MediaReceiptPhase::Planned,ledger,stop);
        check(planned.bytes()==original.bytes(),"Committed receipt differs from original intent");
    }
    auto file=root.open(prefix+"media.wav",maximum,stop);
    check(file.size()==receipt->data().audio.sourceBytes,"Committed media extent differs");
    const auto decoded=validateApprovedWave(file,{},stop);
    check(audioEqual(decoded,receipt->data().audio),"Committed media/format differs from receipt");
    file.verifyUnchanged();return {MediaRecoveryPhase::Verified,std::move(receipt)};
}
} // namespace soundcurrent::daw
