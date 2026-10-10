// SPDX-License-Identifier: GPL-3.0-only
// Isolated control/disk process only. Never linked into the callback graph.
#include <soundcurrent/media_cache.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/stretch.hpp>
#include <soundcurrent/stretch_render_protocol.hpp>
#include <soundcurrent/protected_warp_render.hpp>
#include "warp_codec.hpp"
#include <soundcurrent/project_store.hpp>
#include "media_io.hpp"
#include <rubberband/RubberBandStretcher.h>
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <system_error>
#include <cmath>
#include <iostream>
#include <numeric>
#include <memory>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif
using namespace soundcurrent::daw;
using Json=nlohmann::json;
namespace {
void require(bool ok,const char *message,ErrorCode code=ErrorCode::InvalidParameter){if(!ok)throw ProjectError(code,message);}
std::uint64_t decimal(const std::string &s){std::uint64_t n=0;auto r=std::from_chars(s.data(),s.data()+s.size(),n);require(r.ec==std::errc{} && r.ptr==s.data()+s.size(),"Invalid trusted worker limit");return n;}
void processLimit(std::uint64_t bytes){
#ifdef _WIN32
    // Process-lifetime handle: closing a containing job early is unsafe.
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const auto job=CreateJobObjectW(nullptr,nullptr);require(job!=nullptr,"Cannot create stretch memory job",ErrorCode::ResourceLimit);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;info.ProcessMemoryLimit=SIZE_T(bytes);
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info)) || !AssignProcessToJobObject(job,GetCurrentProcess())){
        CloseHandle(job);throw ProjectError(ErrorCode::ResourceLimit,"Cannot enforce stretch process memory limit");
    }
#else
    rlimit limit{rlim_t(bytes),rlim_t(bytes)};require(setrlimit(RLIMIT_AS,&limit)==0,"Cannot enforce stretch address-space limit",ErrorCode::ResourceLimit);
    // A failed child must not fill its filesystem with a core dump.
    rlimit core{0,0};require(setrlimit(RLIMIT_CORE,&core)==0,"Cannot disable stretch core dump",ErrorCode::ResourceLimit);
#endif
}
std::uint64_t number(const Json &j,const char *key,std::uint64_t maximum){const auto &v=j.at(key);require(v.is_number_unsigned() || (v.is_number_integer() && v.get<std::int64_t>()>=0),"Invalid unsigned stretch field");auto n=v.get<std::uint64_t>();require(n<=maximum,"Stretch field exceeds admitted bounds");return n;}
Json request(std::size_t &n){
    std::vector<char> raw(warpProtocolMaximum+1);n=0;char c;
    while(std::cin.get(c)){require(n<warpProtocolMaximum,"Stretch request exceeds protocol bound");raw[n++]=c;}
    std::array<std::set<std::string>,9> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json &v){require(depth>=0 && depth<8,"Stretch request nesting exceeds bound");if(event==Json::parse_event_t::object_start)keys[std::size_t(depth+1)].clear();if(event==Json::parse_event_t::key)require(keys[std::size_t(depth)].insert(v.get<std::string>()).second,"Duplicate stretch request field");return true;};
    auto j=Json::parse(raw.data(),raw.data()+n,callback);require(j.is_object() && (j.size()==20 || j.size()==21),"Unexpected stretch request fields");return j;
}
std::filesystem::path native(const std::string &s){require(s.size()<=4096 && validUtf8(s) && s.find('\0')==std::string::npos,"Invalid stretch root");return utf8Path(s);}
void plainAncestors(const std::filesystem::path &p){require(p.is_absolute() && p==p.lexically_normal(),"Stretch root must be canonical absolute path");auto at=p.root_path();media_io::plainDirectory(at);for(const auto &part:p.relative_path()){at/=part;media_io::plainDirectory(at);}}
struct Wave {
    media_io::File descriptor;
    SNDFILE *file=nullptr;
    Wave(const std::filesystem::path &path,std::uint32_t rate,std::uint32_t channels):descriptor(path,true){SF_INFO info{};info.samplerate=int(rate);info.channels=int(channels);info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;file=sf_open_fd(descriptor.descriptor(),SFM_WRITE,&info,SF_FALSE);require(file!=nullptr,"Cannot create stretch RF64",ErrorCode::Io);}
    ~Wave(){if(file)sf_close(file);}
    void close(){auto *f=file;file=nullptr;require(sf_close(f)==0,"Cannot finalize stretch RF64",ErrorCode::Io);descriptor.flush();descriptor.close();}
};
int run(const std::vector<std::string> &args){
    const char *protocol="sc-stretch-render-v4";
    bool publicationMayHaveCommitted=false;
    try{
        require(args.size()==13 && args[1]=="--project-root" && args[3]=="--jobs-root" && args[5]=="--memory-mib" && args[7]=="--maximum-input-frames" && args[9]=="--maximum-output-bytes" && args[11]=="--deadline-ms","Invalid stretch worker arguments");
        auto mib=decimal(args[6]),maximumInput=decimal(args[8]),maximumBytes=decimal(args[10]);
        require(mib>=16 && mib<=4096 && maximumInput>0 && maximumInput<=1000000000 && maximumBytes>=4096,"Invalid stretch worker policy");
        auto deadline=decimal(args[12]);require(deadline>=100 && deadline<=3600000,"Invalid stretch deadline");
        processLimit(mib*1024*1024);
        ResourceLedger ledger(std::size_t(mib*1024*1024),"Stretch worker owned payload");
        auto codecLease=ledger.reserve(warpProtocolMaximum*12);
        // This watchdog belongs to the disposable worker process. The OS
        // reaps it on ordinary exit; expiration stops even a blocked vendor call.
        std::thread([deadline]{std::this_thread::sleep_for(std::chrono::milliseconds(deadline));std::_Exit(2);}).detach();
        std::size_t requestBytes=0;auto j=request(requestBytes);
        const auto requestedProtocol=j.at("protocol").get<std::string>();
        const bool protectedMode=requestedProtocol==warpRenderProtocol;
        require(protectedMode || requestedProtocol==stretchRenderProtocol,"Unsupported stretch protocol",ErrorCode::UnsupportedSchema);
        protocol=protectedMode?"sc-stretch-render-v5":"sc-stretch-render-v4";
        require((protectedMode && j.size()==21 && j.contains("warp")) || (!protectedMode && j.size()==20 && !j.contains("warp") && requestBytes<=stretchProtocolMaximum),"Stretch request differs from its protocol envelope");
        const auto operation=Id(j.at("operation").get<std::string>());
        Asset asset{Id(j.at("assetId").get<std::string>()),j.at("relative").get<std::string>(),j.at("sha256").get<std::string>(),48000,{},0};
        require(asset.sha256.size()==64 && std::all_of(asset.sha256.begin(),asset.sha256.end(),[](char c){return(c>='0' && c<='9')||(c>='a' && c<='f');}),"Invalid source digest");
        asset.sampleRate=std::uint32_t(number(j,"rate",384000));require(asset.sampleRate>=8000,"Invalid stretch physical rate");
        asset.layout.channels=std::uint32_t(number(j,"channels",256));require(asset.layout.channels>0,"Invalid stretch channel count");asset.layout.kind=asset.layout.channels==1?LayoutKind::Mono:asset.layout.channels==2?LayoutKind::Stereo:LayoutKind::Discrete;
        asset.frames=Frame(number(j,"sourceFrames",INT64_MAX));const auto first=number(j,"first",std::uint64_t(asset.frames)),selectedFrames=number(j,"frames",maximumInput);
        require(selectedFrames>0 && selectedFrames<=std::uint64_t(asset.frames)-first,"Invalid stretch source span");
        auto fraction=number(j,"firstFraction",UINT64_MAX),fractionDenominator=number(j,"firstDenominator",UINT64_MAX);
        require(fractionDenominator && fraction<fractionDenominator,"Invalid exact stretch source origin");
        const auto fractionDivisor=std::gcd(fraction,fractionDenominator);fraction/=fractionDivisor;fractionDenominator/=fractionDivisor;
        auto numerator=number(j,"timeNumerator",1000000),denominator=number(j,"timeDenominator",1000000);require(numerator && denominator && numerator*4>=denominator && denominator*4>=numerator,"Invalid constant stretch ratio");auto gcd=std::gcd(numerator,denominator);numerator/=gcd;denominator/=gcd;
        auto cents=j.at("pitchMilliCents");require(cents.is_number_integer(),"Pitch must use stable integer milli-cents");if(cents.is_number_unsigned())require(cents.get<std::uint64_t>()<=2400000,"Pitch integer exceeds range");auto pitch=cents.get<std::int64_t>();require(pitch>=-2400000 && pitch<=2400000,"Invalid pitch range");
        require(j.at("formantPreserved").is_boolean(),"Invalid formant option");bool formant=j.at("formantPreserved").get<bool>();
        const auto processor=j.at("processor").get<std::string>();
        require(j.at("contextEnabled").is_boolean(),"Invalid explicit context mode");
        const auto before=number(j,"contextBefore",1000000000),after=number(j,"contextAfter",1000000000);
        const bool contextEnabled=j.at("contextEnabled").get<bool>();
        require(contextEnabled || (!before && !after),"Context frames require explicit region mode");
        ResourceLease warpLease;
        if(protectedMode){PayloadCharge charge("Worker marker state",std::size_t(mib*1024*1024));charge.add(2048);charge.add(j.at("warp").at("markers").size(),sizeof(WarpAnchor)+1024);warpLease=ledger.reserve(charge.bytes());}
        ClipStretchAnchor anchor;anchor.sourceOrigin={Frame(first),fraction,fractionDenominator};anchor.sourceFrames=Frame(selectedFrames);
        anchor.settings={std::uint32_t(numerator),std::uint32_t(denominator),std::int32_t(pitch),formant};anchor.processor=processor;
        if(contextEnabled)anchor.context=StretchContext{Frame(before),Frame(after)};
        anchor.sourceAssetId=asset.id;anchor.sourceSha256=asset.sha256;
        if(protectedMode)anchor.warp=warp_codec::decode(j.at("warp"),anchor.sourceFrames,stretchOutputFrames(anchor.sourceFrames,anchor.settings),std::size_t(mib*1024*1024),ledger);
        require(processor==stretchProcessorFor(anchor.settings,anchor.context,anchor.warp),"Requested processor differs from render settings",ErrorCode::UnsupportedSchema);
        const auto geometry=stretchGeometry(anchor,asset.frames);
        const auto frames=std::uint64_t(geometry.inputFrames),target=std::uint64_t(geometry.outputFrames);
        require(frames<=maximumInput,"Whole processing region exceeds input frame grant",ErrorCode::ResourceLimit);
        const SourceFrameMap sourceMap(asset.sampleRate,asset.sampleRate,geometry.inputOrigin);
        const PreparedPositionedResampling sourceKernel(sourceMap,asset.layout.channels);
        const bool copy=processor==unityStretchProcessorId || processor==regionCopyProcessorId;
        require(copy || asset.sampleRate<=192000,"Stretch backend above192k remains unsupported",ErrorCode::UnsupportedSchema);
        const auto channels=asset.layout.channels;const auto samples=std::size_t(512)*channels;
        require(target<= (UINT64_MAX-1048576)/(channels*4ULL),"Stretch output size overflow");const auto admittedBytes=target*channels*4ULL+1048576;
        if(admittedBytes>maximumBytes)throw ResourceLimitError("Stretch output file",std::size_t(admittedBytes),std::size_t(maximumBytes));
        const auto sourceWindowSamples=sourceKernel.maximumSourceWindowFrames(512)*channels;
        auto scratchLease=ledger.reserve((samples*3+sourceWindowSamples)*sizeof(float)+65536);
        using RB=RubberBand::RubberBandStretcher;
        auto options=RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|(channels<=2?RB::OptionChannelsTogether:RB::OptionChannelsApart)|(formant?RB::OptionFormantPreserved:RB::OptionFormantShifted);
        std::unique_ptr<RB> rb;
        if(!copy && !protectedMode) {
            rb=std::make_unique<RB>(asset.sampleRate,channels,options,double(target)/double(frames),std::exp2(double(pitch)/1200000.));rb->setDebugLevel(0);rb->setMaxProcessSize(512);rb->setExpectedInputDuration(std::size_t(frames));
        // Conservative offline admission from the prepared public API. Very
        // short R3 spans can drain no audio after its internal startup skip.
        // Refuse before any operation directory/intent, without padding takes.
            require(frames>=rb->getSamplesRequired(),"Source span is shorter than the prepared stretch window",ErrorCode::UnsupportedSchema);
        }
        auto sourceRoot=native(args[2]),jobsRoot=native(args[4]);plainAncestors(sourceRoot);plainAncestors(jobsRoot);
        MediaCacheConfig cacheConfig;cacheConfig.maximumOpenFiles=1;cacheConfig.pageFrames=256;cacheConfig.cacheBudgetBytes=std::size_t(256)*channels*sizeof(float)+256;cacheConfig.registryBudgetBytes=256*1024;cacheConfig.resources=ledger;
        MediaReadCache cache(sourceRoot,std::span<const Asset>(&asset,1),asset.sampleRate,cacheConfig);
        auto admittedKey=encodeStretchRenderKey(asset,anchor,ledger);auto key=Json::parse(admittedKey.bytes());
        const auto renderKey=stretchRenderKey(asset,anchor,ledger);
        std::unique_ptr<ProtectedWarpPlan> protectedPlan;
        if(protectedMode){
            require(!geometry.inputOrigin.fraction,"Protected renderer refuses fractional raw origin",ErrorCode::UnsupportedSchema);
            protectedPlan=std::make_unique<ProtectedWarpPlan>(ClipWarpRegion{geometry.inputOrigin,asset.sampleRate,asset.frames,geometry.inputFrames,geometry.outputFrames,{0,0,1},{geometry.inputFrames,0,1}},anchor.warp->markers,anchor.warp->protection,ledger);
            validateProtectedWarpRender(*protectedPlan,asset.sampleRate,channels);
        }
        auto job=jobsRoot/operation.str();require(std::filesystem::create_directory(job),"Stretch operation already exists",ErrorCode::Io);
        auto cancel=[&]{if(std::filesystem::exists(std::filesystem::symlink_status(job/"cancel.request")))throw ProjectError(ErrorCode::Canceled,"Stretch job canceled before commit");};
        media_io::JobLease writer(job,true);
        auto intent=key;intent["protocol"]=protocol;intent["operation"]=operation.str();intent["complete"]=false;intent["assetId"]=asset.id.str();intent["relative"]=asset.relativePath;intent["sourceFrames"]=asset.frames;media_io::publishJournal(job/"intent.json",intent.dump());
        std::cout<<Json({{"protocol",protocol},{"event","ready"},{"operation",operation.str()},{"renderKey",renderKey}}).dump()<<'\n'<<std::flush;
        // Parent acknowledges the prepared identity before study/process/drain.
        // Cancellation and the hard deadline remain active during this wait.
        while(!std::filesystem::exists(std::filesystem::symlink_status(job/"start.request"))){cancel();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        cancel();
        std::vector<float> interleaved(samples),planar(samples),output(samples),sourceWindow(sourceWindowSamples);std::vector<const float *> inputPointers(channels);std::vector<float *> outputPointers(channels);
        auto read=[&](std::uint64_t pos,std::size_t n){cancel();const auto range=sourceKernel.sourceRange(sourceMap,Frame(pos),std::uint32_t(n),asset.frames);cache.read(0,range.first,{sourceWindow.data(),range.frames*channels});sourceKernel.process(sourceMap,Frame(pos),asset.frames,range.first,{sourceWindow.data(),range.frames*channels},{interleaved.data(),n*channels});for(std::size_t ch=0;ch<channels;++ch){inputPointers[ch]=planar.data()+ch*512;for(std::size_t f=0;f<n;++f){auto x=interleaved[f*channels+ch];require(std::isfinite(x),"Nonfinite stretch source",ErrorCode::MediaMismatch);planar[ch*512+f]=x;}}};
        if(rb)for(std::uint64_t pos=0;pos<frames;pos+=512){auto n=std::size_t(std::min<std::uint64_t>(512,frames-pos));read(pos,n);rb->study(inputPointers.data(),n,pos+n==frames);cancel();}
        Wave wave(job/"audio.partial",asset.sampleRate,channels);media_io::SampleHash sampleHash;std::uint64_t written=0;double peak=0;
        auto write=[&](std::size_t n) {
            require(n<=target-written,"Stretch backend exceeded exact duration",ErrorCode::MediaMismatch);
            for(std::size_t i=0;i<n*channels;++i) {
                const auto x=interleaved[i];require(std::isfinite(x),"Nonfinite stretch output",ErrorCode::MediaMismatch);
                peak=std::max(peak,std::abs(double(x)));
            }
            require(sf_writef_float(wave.file,interleaved.data(),sf_count_t(n))==sf_count_t(n) && sf_error(wave.file)==SF_ERR_NO_ERROR,"Stretch WAV write failed",ErrorCode::Io);
            sampleHash.update({interleaved.data(),n*channels});written+=n;
        };
        auto drain=[&] {
            while(rb->available()>0) {
                cancel();const auto n=std::min(512,rb->available());
                for(std::size_t ch=0;ch<channels;++ch)outputPointers[ch]=output.data()+ch*512;
                const auto got=rb->retrieve(outputPointers.data(),std::size_t(n));
                require(got>0 && got<=std::size_t(n),"Stretch output stalled",ErrorCode::InvalidState);
                for(std::size_t f=0;f<got;++f)for(std::size_t ch=0;ch<channels;++ch)interleaved[f*channels+ch]=output[ch*512+f];
                write(got);
            }
        };
        if(protectedPlan){
            const auto rendered=renderProtectedWarp(*protectedPlan,asset.sampleRate,channels,
                [&](Frame at,std::span<float> samples){const auto n=samples.size()/channels;read(std::uint64_t(at),n);std::copy_n(interleaved.begin(),samples.size(),samples.begin());},
                [&](std::span<const float> samples){std::copy(samples.begin(),samples.end(),interleaved.begin());write(samples.size()/channels);},ledger,cancel);
            require(std::uint64_t(rendered.writtenFrames)==target,"Protected renderer counter differs");
        } else {
        for(std::uint64_t pos=0;pos<frames;pos+=512) {
            const auto n=std::size_t(std::min<std::uint64_t>(512,frames-pos));read(pos,n);
            if(copy)write(n);else {rb->process(inputPointers.data(),n,pos+n==frames);drain();}
            cancel();
        }
        if(rb) {drain();require(rb->available()==-1,"Stretch did not fully drain",ErrorCode::MediaMismatch);}
        }
        require(written==target,"Stretch did not drain to exact duration",ErrorCode::MediaMismatch);wave.close();cancel();
        auto audio=job/"audio.partial";require(std::filesystem::file_size(audio)<=maximumBytes,"Stretch file exceeds admitted byte ceiling",ErrorCode::ResourceLimit);auto hash=hashMediaFile(audio,cancel);require(hashMediaFile(sourceRoot/utf8Path(asset.relativePath),cancel)==asset.sha256,"Source changed during stretch",ErrorCode::MediaMismatch);cancel();
        auto receipt=key;receipt["assetId"]=asset.id.str();receipt["relative"]=asset.relativePath;receipt["sourceFrames"]=asset.frames;receipt["protocol"]=protocol;receipt["operation"]=operation.str();receipt["renderKey"]=renderKey;receipt["complete"]=true;receipt["writtenFrames"]=written;receipt["audioSha256"]=hash;receipt["sampleSha256"]=sampleHash.digest();receipt["peakLinear"]=peak;receipt["memoryCeilingBytes"]=mib*1024*1024;receipt["memoryMetric"]=
#ifdef _WIN32
        "process-commit";
#else
        "address-space";
#endif
        receipt["payloadPeakBytes"]=ledger.usage().peakBytes;receipt["deadlineMilliseconds"]=deadline;receipt["durabilityMinimum"]="file-flushed";
        media_io::publishMedia(audio,job/"audio.wav");cancel();auto receiptText=receipt.dump();publicationMayHaveCommitted=true;media_io::publishJournal(job/"complete.json",receiptText);
        // No cancellation check after commit. A valid marker is the terminal result.
        std::cout<<receiptText<<'\n';return std::cout?0:1;
    }catch(const std::bad_alloc &){std::cerr<<"{\"protocol\":\""<<protocol<<"\",\"complete\":false,\"messageId\":\"stretch.resource_limit\",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;
    }catch(const std::system_error &e){
        // Thread creation under the process ceiling can fail before the vendor
        // allocation, particularly with Debug binaries and larger runtime maps.
        // Preserve an actual resource refusal without treating every OS error
        // as exhaustion. Avoid constructing JSON while memory is constrained.
        const bool resource=e.code()==std::errc::resource_unavailable_try_again || e.code()==std::errc::not_enough_memory;
        std::cerr<<"{\"protocol\":\""<<protocol<<"\",\"complete\":false,\"messageId\":\""<<(resource?"stretch.resource_limit":"stretch.render_failed")<<"\",\"systemErrorCode\":"<<e.code().value()<<",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;
    }catch(const ProjectError &e){std::cerr<<Json({{"protocol",protocol},{"complete",false},{"messageId","stretch.render_failed"},{"errorCode",unsigned(e.code())},{"publicationMayHaveCommitted",publicationMayHaveCommitted}}).dump()<<'\n';return 1;
    }catch(...){std::cerr<<"{\"protocol\":\""<<protocol<<"\",\"complete\":false,\"messageId\":\"stretch.render_failed\",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){std::vector<std::string> args;for(int i=0;i<argc;++i){auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 1;std::string s(std::size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr))return 1;s.pop_back();args.push_back(std::move(s));}return run(args);}
#else
int main(int argc,char **argv){return run({argv,argv+argc});}
#endif
