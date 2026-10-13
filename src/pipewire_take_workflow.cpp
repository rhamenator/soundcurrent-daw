// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_take_workflow.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "media_io.hpp"
#include "pipewire_recording_internal.hpp"
#include "original_prefix_copy_internal.hpp"
#include <algorithm>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace soundcurrent::daw {
PipeWireStoredRecording inspectProjectPipeWireRecording(const std::filesystem::path &projectRoot,const std::filesystem::path &selectedJob,const Id &project,std::optional<ResourceLedger> resources,const std::function<void()> &check) {
    if(check)check();
    const auto root=std::filesystem::absolute(projectRoot).lexically_normal(),job=std::filesystem::absolute(selectedJob).lexically_normal();
    media_io::require(job.parent_path()==root/"media","Selected PipeWire take is outside its project");
    const auto token=job.filename().u8string();const std::string name(reinterpret_cast<const char *>(token.data()),token.size());
    media_io::require(name.starts_with("original-") && name.size()==45,"Selected directory is not an original recording job");const Id asset(name.substr(9));
    media_io::plainDirectory(root);media_io::plainDirectory(root/"media");
    const auto session=ProjectStore(root).load();media_io::require(session.id==project,"PipeWire take project changed");
    auto state=inspectPipeWireRecordingState(job,{},resources,check);
    media_io::require(state.prefix.raw.spec.project==project && state.prefix.raw.spec.asset==asset,"PipeWire take belongs to another project/asset");
    if(check)check();
    return state;
}
PipeWireOriginalPrefixCopyResult copyPipeWireOriginalPrefixWav(const std::filesystem::path &projectRoot,
    const std::filesystem::path &selectedJob,const PipeWireRecordingSpec &expected,
    const std::filesystem::path &destination,const OriginalPrefixCopyOptions &options) {
    const auto check=[&]{if(options.canceled && options.canceled())throw ProjectError(ErrorCode::Canceled,"PipeWire prefix copy canceled");};
    check();const auto root=std::filesystem::absolute(projectRoot).lexically_normal();
    const auto job=std::filesystem::absolute(selectedJob).lexically_normal();
    media_io::require(job.parent_path()==root/"media" && job.filename()=="original-"+expected.raw.asset.str(),"PipeWire copy is outside its project/asset");
    media_io::plainDirectory(root);media_io::plainDirectory(root/"media");media_io::plainDirectory(job);
    const auto session=ProjectStore(root).load();media_io::require(session.id==expected.raw.project,"PipeWire copy project identity differs");
    media_io::JobLease state(job,false,true,media_io::JobLeaseKind::PipeWireState);
    media_io::JobLease receipts(job,false,true,media_io::JobLeaseKind::PipeWireReceipts);
    media_io::JobLease raw(job,false,true);
    media_io::require(state.status()==media_io::LeaseStatus::Held && receipts.status()==media_io::LeaseStatus::Held &&
        raw.status()==media_io::LeaseStatus::Held,"PipeWire prefix copy requires inactive owned roles");
    detail::admitOriginalPrefixCopySource(job,options);
    auto original=detail::inspectPipeWireRecordingStateHeld(job,state,receipts,raw,expected,options.resources,check);
    auto copied=detail::copyOriginalPrefixWavHeld(root,job,original.prefix.raw,raw,destination,options);
    return {std::move(original),std::move(copied)};
}
PipeWireOriginalExportResult exportPipeWireOriginalWav(const std::filesystem::path &projectRoot,
    const std::filesystem::path &job,const PipeWireRecordingSpec &expected,
    const std::filesystem::path &destination,const NativeOriginalExportOptions &options) {
    const auto check=[&]{if(options.canceled && options.canceled())throw ProjectError(ErrorCode::Canceled,"PipeWire original WAV export canceled before publication");};
    check();
    if(!options.maximumBytes || options.maximumBytes>64ULL*1024*1024*1024)
        throw ProjectError(ErrorCode::InvalidParameter,"PipeWire original WAV export byte bound is invalid");
    PayloadCharge charge("PipeWire original WAV copy",options.resources.usage().limitBytes);charge.add(65536+8192);
    auto credit=options.resources.reserve(charge.bytes());
    ApprovedMediaRoot sourceRoot(std::filesystem::absolute(job),options.resources,{1});
    auto source=sourceRoot.open("take.wav",options.maximumBytes);const auto size=source.size();
    const auto before=source.digest({},check);check();
    const auto state=inspectProjectPipeWireRecording(projectRoot,job,expected.raw.project,options.resources,check);
    if(state.intent.expected!=expected)throw ProjectError(ErrorCode::MediaMismatch,"PipeWire export context changed");
    const auto &verified=state.prefix;
    if(!verified.raw.finalized || !verified.raw.committedFrames || verified.raw.finitePrefixFrames!=verified.raw.committedFrames)
        throw ProjectError(ErrorCode::MediaMismatch,"PipeWire original WAV export requires a finalized wholly finite original take");
    // Native recovery verifies a common prefix. Export must also prove that
    // the entire finalized container contains exactly that prefix, with no tail.
    WaveValidationLimits limits;limits.maximumFrames=verified.raw.committedFrames;
    limits.maximumBytesRead=size*4+65536;limits.maximumIoOperations=size+65536;
    limits.maximumChannels=verified.raw.spec.layout.channels;limits.blockFrames=1024;
    const auto wave=validateApprovedWave(source,limits,{}, {},check);
    if(wave.frames!=verified.raw.committedFrames || wave.decodedFrames!=wave.frames ||
        wave.rate!=verified.raw.spec.sampleRate || wave.channels!=verified.raw.spec.layout.channels ||
        wave.encoding!=WaveEncoding::Float32 || wave.sourceSha256!=before)
        throw ProjectError(ErrorCode::MediaMismatch,"Finalized PipeWire original WAV extent or format differs from its verified prefix");
    source.verifyUnchanged();
    if(source.digest({},check)!=before)throw ProjectError(ErrorCode::MediaMismatch,"PipeWire original WAV source changed during original verification");
    const auto approved=inspectExportDestination(projectRoot,destination,check);
    if(approved.exists)throw ProjectError(ErrorCode::InvalidState,"PipeWire original WAV export will not overwrite an existing file");
    if(std::filesystem::absolute(verified.raw.source).lexically_normal()!=std::filesystem::absolute(job/"take.wav").lexically_normal())
        throw ProjectError(ErrorCode::InvalidState,"Finalized native source differs from its owned job");
    const auto temporary=approved.path.parent_path()/utf8Path(".pipewire-wav-"+Id::generate().str()+".partial");
    struct Temporary {std::filesystem::path path;bool owned=false;~Temporary(){if(owned){std::error_code e;std::filesystem::remove(path,e);}}} temp{temporary};
    media_io::File output(temporary,true);temp.owned=true;std::vector<char> buffer(65536);
    for(std::uint64_t offset=0;offset<size;) {
        check();const auto count=std::size_t(std::min<std::uint64_t>(buffer.size(),size-offset));
        source.readAt(offset,{buffer.data(),count});output.write({buffer.data(),count});offset+=count;
        if(options.progress)options.progress(offset,size);
    }
    output.flush();output.close();check();source.verifyUnchanged();
    if(source.digest({},check)!=before || hashMediaFile(temporary,check)!=std::string(before.data(),before.size()))
        throw ProjectError(ErrorCode::MediaMismatch,"PipeWire original WAV bytes changed while copying");
    // Construct the receipt before publication; no later cancellation may hide a published file.
    PipeWireOriginalExportResult result;result.destination=approved.path;
    if(state.outcome)result.disposition=state.outcome->disposition;
    result.bytes=size;result.frames=verified.raw.committedFrames;result.sampleRate=verified.raw.spec.sampleRate;result.layout=verified.raw.spec.layout;
    result.fileSha256.assign(before.data(),before.size());result.sampleSha256=verified.raw.sampleSha256;
    const auto again=inspectExportDestination(projectRoot,approved.path,check);
    if(again.exists)throw ProjectError(ErrorCode::InvalidState,"PipeWire original WAV export destination appeared during copying");
    check();
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),approved.path.c_str(),MOVEFILE_WRITE_THROUGH))
        throw ProjectError(ErrorCode::Io,"Cannot publish PipeWire original WAV without overwriting");
    temp.owned=false;
#else
    if(::link(temporary.c_str(),approved.path.c_str())!=0)
        throw ProjectError(ErrorCode::Io,"Cannot publish PipeWire original WAV without overwriting");
    if(::unlink(temporary.c_str())==0)temp.owned=false;
    else result.publicationWarning="Complete PipeWire original WAV published; temporary alias cleanup failed";
#endif
    try {result.durability=media_io::flushDirectory(approved.path.parent_path());}
    catch(const std::exception &e) {
        result.publicationWarning+=(result.publicationWarning.empty()?"":"; ");result.publicationWarning+=e.what();
    }
    return result;
}
}
