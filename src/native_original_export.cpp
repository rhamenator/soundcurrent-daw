// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/native_original_export.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "media_io.hpp"
#include "native_original_recording_internal.hpp"
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
NativeOriginalPrefixCopyResult copyNativeOriginalPrefixWav(const std::filesystem::path &root,
    const std::filesystem::path &job,const NativeOriginalRecordingSpec &expected,
    const std::filesystem::path &destination,const OriginalPrefixCopyOptions &options) {
    const auto check=[&]{if(options.canceled && options.canceled())
        throw ProjectError(ErrorCode::Canceled,"Native original prefix copy canceled");};
    check();media_io::plainDirectory(job);media_io::JobLease lease(job,false,true);
    media_io::require(lease.status()==media_io::LeaseStatus::Held,"Native prefix copy requires an inactive owned job");
    detail::admitOriginalPrefixCopySource(job,options);
    auto original=detail::inspectNativeOriginalRecordingHeld(job,lease,expected,check,options.resources);
    auto copied=detail::copyOriginalPrefixWavHeld(root,job,original.raw,lease,destination,options);
    return {std::move(original),std::move(copied)};
}
NativeOriginalExportResult exportNativeOriginalWav(const std::filesystem::path &projectRoot,
    const std::filesystem::path &job,const NativeOriginalRecordingSpec &expected,
    const std::filesystem::path &destination,const NativeOriginalExportOptions &options) {
    const auto check=[&]{if(options.canceled && options.canceled())throw ProjectError(ErrorCode::Canceled,"Native WAV export canceled before publication");};
    check();
    if(!options.maximumBytes || options.maximumBytes>64ULL*1024*1024*1024)
        throw ProjectError(ErrorCode::InvalidParameter,"Native WAV export byte bound is invalid");
    PayloadCharge charge("Native WAV copy",options.resources.usage().limitBytes);charge.add(65536+8192);
    auto credit=options.resources.reserve(charge.bytes());
    ApprovedMediaRoot sourceRoot(std::filesystem::absolute(job),options.resources,{1});
    auto source=sourceRoot.open("take.wav",options.maximumBytes);const auto size=source.size();
    const auto before=source.digest({},check);check();
    const auto verified=inspectNativeOriginalRecording(job,expected,check,options.resources);
    if(!verified.raw.finalized || !verified.raw.committedFrames || verified.raw.finitePrefixFrames!=verified.raw.committedFrames)
        throw ProjectError(ErrorCode::MediaMismatch,"Native WAV export requires a finalized wholly finite original take");
    // Native recovery verifies a common prefix. Export must also prove that
    // the entire finalized container contains exactly that prefix, with no tail.
    WaveValidationLimits limits;limits.maximumFrames=verified.raw.committedFrames;
    limits.maximumBytesRead=size*4+65536;limits.maximumIoOperations=size+65536;
    limits.maximumChannels=verified.raw.spec.layout.channels;limits.blockFrames=1024;
    const auto wave=validateApprovedWave(source,limits,{}, {},check);
    if(wave.frames!=verified.raw.committedFrames || wave.decodedFrames!=wave.frames ||
        wave.rate!=verified.raw.spec.sampleRate || wave.channels!=verified.raw.spec.layout.channels ||
        wave.encoding!=WaveEncoding::Float32 || wave.sourceSha256!=before)
        throw ProjectError(ErrorCode::MediaMismatch,"Finalized native WAV extent or format differs from its verified prefix");
    source.verifyUnchanged();
    if(source.digest({},check)!=before)throw ProjectError(ErrorCode::MediaMismatch,"Native WAV source changed during original verification");
    const auto approved=inspectExportDestination(projectRoot,destination,check);
    if(approved.exists)throw ProjectError(ErrorCode::InvalidState,"Native WAV export will not overwrite an existing file");
    if(std::filesystem::absolute(verified.raw.source).lexically_normal()!=std::filesystem::absolute(job/"take.wav").lexically_normal())
        throw ProjectError(ErrorCode::InvalidState,"Finalized native source differs from its owned job");
    const auto temporary=approved.path.parent_path()/utf8Path(".native-wav-"+Id::generate().str()+".partial");
    struct Temporary {std::filesystem::path path;bool owned=false;~Temporary(){if(owned){std::error_code e;std::filesystem::remove(path,e);}}} temp{temporary};
    media_io::File output(temporary,true);temp.owned=true;std::vector<char> buffer(65536);
    for(std::uint64_t offset=0;offset<size;) {
        check();const auto count=std::size_t(std::min<std::uint64_t>(buffer.size(),size-offset));
        source.readAt(offset,{buffer.data(),count});output.write({buffer.data(),count});offset+=count;
        if(options.progress)options.progress(offset,size);
    }
    output.flush();output.close();check();source.verifyUnchanged();
    if(source.digest({},check)!=before || hashMediaFile(temporary,check)!=std::string(before.data(),before.size()))
        throw ProjectError(ErrorCode::MediaMismatch,"Native WAV bytes changed while copying");
    // Construct the receipt before publication; no later cancellation may hide a published file.
    NativeOriginalExportResult result;result.destination=approved.path;result.originalStatus=verified.status;
    result.bytes=size;result.frames=verified.raw.committedFrames;result.sampleRate=verified.raw.spec.sampleRate;result.layout=verified.raw.spec.layout;
    result.fileSha256.assign(before.data(),before.size());result.sampleSha256=verified.raw.sampleSha256;
    const auto again=inspectExportDestination(projectRoot,approved.path,check);
    if(again.exists)throw ProjectError(ErrorCode::InvalidState,"Native WAV export destination appeared during copying");
    check();
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),approved.path.c_str(),MOVEFILE_WRITE_THROUGH))
        throw ProjectError(ErrorCode::Io,"Cannot publish native WAV without overwriting");
    temp.owned=false;
#else
    if(::link(temporary.c_str(),approved.path.c_str())!=0)
        throw ProjectError(ErrorCode::Io,"Cannot publish native WAV without overwriting");
    if(::unlink(temporary.c_str())==0)temp.owned=false;
    else result.publicationWarning="Complete native WAV published; temporary alias cleanup failed";
#endif
    try {result.durability=media_io::flushDirectory(approved.path.parent_path());}
    catch(const std::exception &e) {
        result.publicationWarning+=(result.publicationWarning.empty()?"":"; ");result.publicationWarning+=e.what();
    }
    return result;
}
}
