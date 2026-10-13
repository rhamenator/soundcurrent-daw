// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/original_prefix_copy.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include "original_prefix_copy_internal.hpp"
#include "original_recording_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace soundcurrent::daw {
namespace {
void checkBounds(const OriginalPrefixCopyOptions &o) {
    if (!o.maximumBytes || o.maximumBytes>64ULL*1024*1024*1024)
        throw ProjectError(ErrorCode::InvalidParameter,"Original prefix copy byte bound is invalid");
}
void poll(const OriginalPrefixCopyOptions &o) {
    if(o.canceled && o.canceled())throw ProjectError(ErrorCode::Canceled,"Original prefix copy canceled before publication");
}
std::array<char,92> header(const OriginalRecordingSpec &s,std::uint64_t frames) {
    std::array<char,92> h{};
    const auto tag=[&](std::size_t at,std::string_view v){std::copy(v.begin(),v.end(),h.begin()+std::ptrdiff_t(at));};
    const auto number=[&](std::size_t at,std::size_t width,std::uint64_t v){for(std::size_t n=0;n<width;++n)h[at+n]=char((v>>(8*n))&255);};
    const auto bytes=frames*s.layout.channels*4;
    tag(0,"RF64");number(4,4,UINT32_MAX);tag(8,"WAVE");tag(12,"ds64");number(16,4,28);
    number(20,8,bytes+84);number(28,8,bytes);number(36,8,frames);number(44,4,0);
    tag(48,"fmt ");number(52,4,16);number(56,2,3);number(58,2,s.layout.channels);
    number(60,4,s.sampleRate);number(64,4,std::uint64_t(s.sampleRate)*s.layout.channels*4);
    number(68,2,s.layout.channels*4);number(70,2,32);
    tag(72,"fact");number(76,4,4);number(80,4,std::min<std::uint64_t>(frames,UINT32_MAX));
    tag(84,"data");number(88,4,UINT32_MAX);return h;
}
}
void detail::admitOriginalPrefixCopySource(const std::filesystem::path &job,const OriginalPrefixCopyOptions &options) {
    checkBounds(options);poll(options);
    ApprovedMediaRoot root(std::filesystem::absolute(job),options.resources,{2});
    bool found=false;
    for(const auto name:{"audio.partial.rf64","take.wav"}) {
        if(!std::filesystem::exists(std::filesystem::symlink_status(job/name)))continue;
        auto file=root.open(name,options.maximumBytes);file.verifyUnchanged();found=true;
    }
    media_io::require(found,"Original prefix copy media is missing");
}
OriginalPrefixCopyResult detail::copyOriginalPrefixWavHeld(const std::filesystem::path &projectRoot,
    const std::filesystem::path &job,const OriginalRecordingPrefix &verified,const media_io::JobLease &lease,
    const std::filesystem::path &destination,const OriginalPrefixCopyOptions &options) {
    checkBounds(options);poll(options);
    media_io::require(lease.status()==media_io::LeaseStatus::Held,"Prefix copy requires an exclusive inactive raw job");
    const auto root=std::filesystem::absolute(projectRoot).lexically_normal();
    const auto ownedJob=std::filesystem::absolute(job).lexically_normal();
    const auto sourcePath=std::filesystem::absolute(verified.source).lexically_normal();
    media_io::require(ownedJob.parent_path()==root/"media" &&
        ownedJob.filename()=="original-"+verified.spec.asset.str() && sourcePath.parent_path()==ownedJob &&
        (sourcePath.filename()=="take.wav" || sourcePath.filename()=="audio.partial.rf64"),"Prefix copy source is outside its owned job");
    media_io::require(verified.committedFrames && verified.finitePrefixFrames &&
        verified.finitePrefixFrames<=verified.committedFrames,"Original has no verified playable prefix");
    const auto frameBytes=std::uint64_t(verified.spec.layout.channels)*4;
    media_io::require(frameBytes && frameBytes<=1024 && verified.finitePrefixFrames<=(options.maximumBytes-std::min<std::uint64_t>(options.maximumBytes,92))/frameBytes,
        "Copied original prefix exceeds byte bound");
    auto work=options.resources.reserve(128*1024);
    ApprovedMediaRoot approvedRoot(ownedJob,options.resources,{1});
    auto source=approvedRoot.openSelectedFilename(sourcePath.filename(),options.maximumBytes);
    const auto check=[&]{poll(options);};
    const auto before=source.digest({},check);
    WaveValidationLimits limits;limits.maximumFrames=options.maximumBytes/4;
    limits.maximumBytesRead=options.maximumBytes*4+65536;limits.maximumIoOperations=options.maximumBytes+65536;
    limits.maximumChannels=verified.spec.layout.channels;
    const auto extent=inspectApprovedWaveData(source,limits,WaveExtentPolicy::PrefixContainer,check);
    media_io::require(extent.rf64 && !extent.format.bigEndian && extent.format.encoding==WaveEncoding::Float32 &&
        extent.format.rate==verified.spec.sampleRate && extent.format.channels==verified.spec.layout.channels &&
        extent.format.frames>=verified.committedFrames,"Original prefix copy container/format differs");
    const auto approved=inspectExportDestination(root,destination,check);
    if(approved.exists)throw ProjectError(ErrorCode::InvalidState,"Original prefix copy will not overwrite an existing file");
    const auto temporary=approved.path.parent_path()/utf8Path(".original-prefix-"+Id::generate().str()+".partial");
    struct Temporary {std::filesystem::path path;bool owned=false;~Temporary(){if(owned){std::error_code e;std::filesystem::remove(path,e);}}} temp{temporary};
    media_io::File output(temporary,true);temp.owned=true;
    const auto h=header(verified.spec,verified.finitePrefixFrames);output.write({h.data(),h.size()});
    std::array<char,65536> buffer{};media_io::SampleHash full,selected;
    const auto total=verified.committedFrames*frameBytes,copyBytes=verified.finitePrefixFrames*frameBytes;
    for(std::uint64_t at=0;at<total;) {
        check();const auto count=std::size_t(std::min<std::uint64_t>((buffer.size()/frameBytes)*frameBytes,total-at));
        source.readAt(extent.offset+at,{buffer.data(),count});
        full.updateBytes(std::as_bytes(std::span(buffer).first(count)));
        const auto written=at<copyBytes?std::size_t(std::min<std::uint64_t>(count,copyBytes-at)):0;
        for(std::size_t n=0;n<written;n+=4) {
            std::uint32_t bits=0;for(unsigned k=0;k<4;++k)bits|=std::uint32_t(static_cast<unsigned char>(buffer[n+k]))<<(8*k);
            media_io::require(std::isfinite(std::bit_cast<float>(bits)),"Verified playable original prefix changed");
        }
        if(written){output.write({buffer.data(),written});selected.updateBytes(std::as_bytes(std::span(buffer).first(written)));}
        at+=count;if(options.progress)options.progress(at,total);
    }
    media_io::require(full.digest()==verified.sampleSha256,"Original common prefix changed while copying");
    output.flush();output.close();check();source.verifyUnchanged();
    media_io::require(source.digest({},check)==before,"Original media changed while copying");
    OriginalPrefixCopyResult result;result.original=verified;result.destination=approved.path;
    result.frames=verified.finitePrefixFrames;result.bytes=92+copyBytes;result.sampleSha256=selected.digest();
    result.fileSha256=hashMediaFile(temporary,check);
    const auto again=inspectExportDestination(root,approved.path,check);
    if(again.exists)throw ProjectError(ErrorCode::InvalidState,"Original prefix copy destination appeared during copying");
    check();
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),approved.path.c_str(),MOVEFILE_WRITE_THROUGH))
        throw ProjectError(ErrorCode::Io,"Cannot publish original prefix without overwriting");
    temp.owned=false;
#else
    if(::link(temporary.c_str(),approved.path.c_str())!=0)
        throw ProjectError(ErrorCode::Io,"Cannot publish original prefix without overwriting");
    if(::unlink(temporary.c_str())==0)temp.owned=false;
    else result.publicationWarning="Original prefix published; temporary alias cleanup failed";
#endif
    try{result.durability=media_io::flushDirectory(approved.path.parent_path());}
    catch(const std::exception &e){result.publicationWarning+=(result.publicationWarning.empty()?"":"; ");result.publicationWarning+=e.what();}
    return result;
}
OriginalPrefixCopyResult copyOriginalPrefixWav(const std::filesystem::path &root,const std::filesystem::path &job,
    const OriginalRecordingSpec &expected,const std::filesystem::path &destination,const OriginalPrefixCopyOptions &options) {
    checkBounds(options);const auto check=[&]{poll(options);};check();
    media_io::plainDirectory(job);media_io::JobLease lease(job,false,true);
    media_io::require(lease.status()==media_io::LeaseStatus::Held,"Original writer active or lease missing");
    detail::admitOriginalPrefixCopySource(job,options);
    const auto prefix=detail::inspectOriginalRecordingHeld(job,lease,expected,check,options.resources);
    return detail::copyOriginalPrefixWavHeld(root,job,prefix,lease,destination,options);
}
}
