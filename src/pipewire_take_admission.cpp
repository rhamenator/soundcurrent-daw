// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_take_workflow.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <limits>
namespace soundcurrent::daw {
VerifiedNativeTake verifyPipeWireTakeForAdmission(const std::filesystem::path &root,const std::filesystem::path &job,
    const PipeWireRecordingSpec &expected,const Id &target,Frame start,std::uint32_t rate,const Id &clipId,
    const NativeTakeAdmissionOptions &options) {
    const auto check=options.beforeRead;if(check)check();
    if(start<0 || rate<8000 || rate>384000 || !options.maximumAudioBytes || options.maximumAudioBytes>64ULL*1024*1024*1024)
        throw ProjectError(ErrorCode::InvalidParameter,"PipeWire take placement or byte policy is invalid");
    PayloadCharge charge("Verified native take",options.resources.usage().limitBytes);charge.add(32768);charge.add(root.native().size()+1,sizeof(std::filesystem::path::value_type));
    auto lease=options.resources.reserve(charge.bytes());
    const auto ownedJob=std::filesystem::absolute(job).lexically_normal();
    const auto ownedRoot=std::filesystem::absolute(root).lexically_normal();
    if(ownedJob!=ownedRoot/"media"/("original-"+expected.raw.asset.str()))
        throw ProjectError(ErrorCode::InvalidParameter,"PipeWire admission source is not its owned project job");
    const auto initial=inspectProjectPipeWireRecording(root,ownedJob,expected.raw.project,options.resources,check);
    if(initial.intent.expected!=expected)throw ProjectError(ErrorCode::MediaMismatch,"PipeWire admission context changed");
    ApprovedMediaRoot sourceRoot(ownedJob,options.resources,{10});
    auto audio=sourceRoot.open("take.wav",options.maximumAudioBytes);const auto audioHash=audio.digest({},check);
    const auto &names=initial.evidenceFiles;
    std::vector<ApprovedMediaFile> pins;std::vector<ProjectEvidenceFile> evidence;
    const auto relative="media/original-"+expected.raw.asset.str()+"/";
    for(const auto &name:names) {
        auto file=sourceRoot.open(name,(name=="packets.bin" || name.ends_with("-cycles.bin"))?64ULL*1024*1024*1024:4*1024*1024);
        const auto hash=file.digest({},check);evidence.push_back({relative+name,std::string(hash.data(),hash.size()),file.size()});pins.push_back(std::move(file));
    }
    const auto current=inspectProjectPipeWireRecording(root,ownedJob,expected.raw.project,options.resources,check);
    const auto &prefix=current.prefix;
    if(current.evidenceFiles!=names)throw ProjectError(ErrorCode::MediaMismatch,"PipeWire artifact references changed during admission");
    if(current.intent.expected!=expected || !prefix.finalized || !prefix.raw.finalized || !prefix.raw.committedFrames ||
        prefix.raw.finitePrefixFrames!=prefix.raw.committedFrames || prefix.raw.committedFrames>std::uint64_t(INT64_MAX))
        throw ProjectError(ErrorCode::MediaMismatch,"PipeWire admission requires the exact finalized wholly finite original");
    WaveValidationLimits limits;limits.maximumFrames=prefix.raw.committedFrames;limits.maximumChannels=expected.raw.layout.channels;
    limits.maximumBytesRead=audio.size()*4+65536;limits.maximumIoOperations=audio.size()+65536;limits.blockFrames=1024;
    const auto wave=validateApprovedWave(audio,limits,{}, {},check);
    if(wave.frames!=prefix.raw.committedFrames || wave.sourceSha256!=audioHash || wave.rate!=expected.raw.sampleRate ||
        wave.channels!=expected.raw.layout.channels || wave.encoding!=WaveEncoding::Float32)
        throw ProjectError(ErrorCode::MediaMismatch,"PipeWire WAV differs from its committed source extent");
    for(std::size_t i=0;i<pins.size();++i) {
        pins[i].verifyUnchanged();const auto hash=pins[i].digest({},check);
        if(std::string(hash.data(),hash.size())!=evidence[i].sha256)
            throw ProjectError(ErrorCode::MediaMismatch,"PipeWire evidence changed during admission verification");
    }
    audio.verifyUnchanged();
    // Reopen the named paths as well as checking pinned handles: a replaced
    // directory entry must not silently become the project's future source.
    for(std::size_t i=0;i<names.size();++i){auto named=sourceRoot.open(names[i],evidence[i].bytes);const auto digest=named.digest({},check);
        if(named.size()!=evidence[i].bytes || std::string(digest.data(),digest.size())!=evidence[i].sha256)
            throw ProjectError(ErrorCode::MediaMismatch,"PipeWire evidence path replaced during admission");}
    {auto named=sourceRoot.open("take.wav",options.maximumAudioBytes);if(named.digest({},check)!=audioHash)throw ProjectError(ErrorCode::MediaMismatch,"PipeWire WAV path replaced during admission");}
    Asset asset;asset.id=expected.raw.asset;asset.relativePath=relative+"take.wav";
    asset.sha256.assign(audioHash.data(),audioHash.size());asset.sampleRate=wave.rate;asset.layout=expected.raw.layout;
    asset.frames=Frame(wave.frames);asset.nativeEvidence=std::move(evidence);
    Clip clip;clip.id=clipId;clip.assetId=asset.id;clip.startFrame=start;
    clip.lengthFrames=SourceFrameMap(asset.sampleRate,rate).projectFramesForSource(asset.frames);
    if(!clip.lengthFrames || start>INT64_MAX-clip.lengthFrames)
        throw ProjectError(ErrorCode::InvalidParameter,"Native clip project range overflows");
    if(check)check();
    return VerifiedNativeTake(std::move(lease),root,expected.raw.project,{target,std::move(asset),std::move(clip)});
}
}
