// SPDX-License-Identifier: GPL-3.0-only
// Isolated control/disk process only. Never linked into the callback graph.
#include <soundcurrent/media_cache.hpp>
#include <soundcurrent/clip_timing.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <soundcurrent/stretch.hpp>
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
constexpr const char *protocol="sc-stretch-render-v2";
constexpr auto processor=stretchProcessorId;
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
Json request(){
    std::array<char,16385> raw{};std::size_t n=0;char c;
    while(std::cin.get(c)){require(n<16384,"Stretch request exceeds protocol bound");raw[n++]=c;}
    std::array<std::set<std::string>,9> keys;
    auto callback=[&](int depth,Json::parse_event_t event,Json &v){require(depth>=0 && depth<8,"Stretch request nesting exceeds bound");if(event==Json::parse_event_t::object_start)keys[std::size_t(depth+1)].clear();if(event==Json::parse_event_t::key)require(keys[std::size_t(depth)].insert(v.get<std::string>()).second,"Duplicate stretch request field");return true;};
    auto j=Json::parse(raw.data(),raw.data()+n,callback);require(j.is_object() && j.size()==16,"Unexpected stretch request fields");return j;
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
    bool publicationMayHaveCommitted=false;
    try{
        require(args.size()==13 && args[1]=="--project-root" && args[3]=="--jobs-root" && args[5]=="--memory-mib" && args[7]=="--maximum-input-frames" && args[9]=="--maximum-output-bytes" && args[11]=="--deadline-ms","Invalid stretch worker arguments");
        auto mib=decimal(args[6]),maximumInput=decimal(args[8]),maximumBytes=decimal(args[10]);
        require(mib>=16 && mib<=4096 && maximumInput>0 && maximumInput<=1000000000 && maximumBytes>=4096,"Invalid stretch worker policy");
        auto deadline=decimal(args[12]);require(deadline>=100 && deadline<=3600000,"Invalid stretch deadline");
        processLimit(mib*1024*1024);
        // This watchdog belongs to the disposable worker process. The OS
        // reaps it on ordinary exit; expiration stops even a blocked vendor call.
        std::thread([deadline]{std::this_thread::sleep_for(std::chrono::milliseconds(deadline));std::_Exit(2);}).detach();
        auto j=request();require(j.at("protocol")==protocol,"Unsupported stretch protocol",ErrorCode::UnsupportedSchema);
        const auto operation=Id(j.at("operation").get<std::string>());
        Asset asset{Id(j.at("assetId").get<std::string>()),j.at("relative").get<std::string>(),j.at("sha256").get<std::string>(),48000,{},0};
        require(asset.sha256.size()==64 && std::all_of(asset.sha256.begin(),asset.sha256.end(),[](char c){return(c>='0' && c<='9')||(c>='a' && c<='f');}),"Invalid source digest");
        asset.sampleRate=std::uint32_t(number(j,"rate",384000));require(asset.sampleRate>=8000,"Invalid stretch physical rate");
        require(asset.sampleRate<=192000,"Stretch backend above192k remains unsupported",ErrorCode::UnsupportedSchema);
        asset.layout.channels=std::uint32_t(number(j,"channels",256));require(asset.layout.channels>0,"Invalid stretch channel count");asset.layout.kind=asset.layout.channels==1?LayoutKind::Mono:asset.layout.channels==2?LayoutKind::Stereo:LayoutKind::Discrete;
        asset.frames=Frame(number(j,"sourceFrames",INT64_MAX));const auto first=number(j,"first",std::uint64_t(asset.frames)),frames=number(j,"frames",maximumInput);
        require(frames>0 && frames<=std::uint64_t(asset.frames)-first,"Invalid stretch source span");
        auto fraction=number(j,"firstFraction",UINT64_MAX),fractionDenominator=number(j,"firstDenominator",UINT64_MAX);
        require(fractionDenominator && fraction<fractionDenominator,"Invalid exact stretch source origin");
        const auto fractionDivisor=std::gcd(fraction,fractionDenominator);fraction/=fractionDivisor;fractionDenominator/=fractionDivisor;
        const SourceFrameMap sourceMap(asset.sampleRate,asset.sampleRate,SourcePosition{Frame(first),fraction,fractionDenominator});
        const PreparedPositionedResampling sourceKernel(sourceMap,asset.layout.channels);
        auto numerator=number(j,"timeNumerator",1000000),denominator=number(j,"timeDenominator",1000000);require(numerator && denominator && numerator*4>=denominator && denominator*4>=numerator,"Invalid constant stretch ratio");auto gcd=std::gcd(numerator,denominator);numerator/=gcd;denominator/=gcd;
        const auto product=frames*numerator,target=product/denominator+(product%denominator!=0);
        require(target>0 && target<=1000000000,"Stretch target exceeds exact admitted duration");
        auto cents=j.at("pitchMilliCents");require(cents.is_number_integer(),"Pitch must use stable integer milli-cents");if(cents.is_number_unsigned())require(cents.get<std::uint64_t>()<=2400000,"Pitch integer exceeds range");auto pitch=cents.get<std::int64_t>();require(pitch>=-2400000 && pitch<=2400000,"Invalid pitch range");
        require(j.at("formantPreserved").is_boolean(),"Invalid formant option");bool formant=j.at("formantPreserved").get<bool>();
        const auto channels=asset.layout.channels;const auto samples=std::size_t(512)*channels;
        require(target<= (UINT64_MAX-1048576)/(channels*4ULL),"Stretch output size overflow");const auto admittedBytes=target*channels*4ULL+1048576;
        if(admittedBytes>maximumBytes)throw ResourceLimitError("Stretch output file",std::size_t(admittedBytes),std::size_t(maximumBytes));
        ResourceLedger ledger(std::size_t(mib*1024*1024),"Stretch worker owned payload");const auto sourceWindowSamples=sourceKernel.maximumSourceWindowFrames(512)*channels;
        auto scratchLease=ledger.reserve((samples*3+sourceWindowSamples)*sizeof(float)+65536);
        using RB=RubberBand::RubberBandStretcher;
        auto options=RB::OptionProcessOffline|RB::OptionEngineFiner|RB::OptionThreadingNever|(channels<=2?RB::OptionChannelsTogether:RB::OptionChannelsApart)|(formant?RB::OptionFormantPreserved:RB::OptionFormantShifted);
        RB rb(asset.sampleRate,channels,options,double(target)/double(frames),std::exp2(double(pitch)/1200000.));rb.setDebugLevel(0);rb.setMaxProcessSize(512);rb.setExpectedInputDuration(std::size_t(frames));
        // Conservative offline admission from the prepared public API. Very
        // short R3 spans can drain no audio after its internal startup skip.
        // Refuse before any operation directory/intent, without padding takes.
        require(frames>=rb.getSamplesRequired(),"Source span is shorter than the prepared stretch window",ErrorCode::UnsupportedSchema);
        auto sourceRoot=native(args[2]),jobsRoot=native(args[4]);plainAncestors(sourceRoot);plainAncestors(jobsRoot);
        MediaCacheConfig cacheConfig;cacheConfig.maximumOpenFiles=1;cacheConfig.pageFrames=256;cacheConfig.cacheBudgetBytes=std::size_t(256)*channels*sizeof(float)+256;cacheConfig.registryBudgetBytes=256*1024;cacheConfig.resources=ledger;
        MediaReadCache cache(sourceRoot,std::span<const Asset>(&asset,1),asset.sampleRate,cacheConfig);
        auto job=jobsRoot/operation.str();require(std::filesystem::create_directory(job),"Stretch operation already exists",ErrorCode::Io);
        auto cancel=[&]{if(std::filesystem::exists(std::filesystem::symlink_status(job/"cancel.request")))throw ProjectError(ErrorCode::Canceled,"Stretch job canceled before commit");};
        Json key={{"processor",processor},{"sourceSha256",asset.sha256},{"rate",asset.sampleRate},{"channels",channels},{"first",first},{"firstFraction",fraction},{"firstDenominator",fractionDenominator},{"sourceAlgorithm",positionedResamplingAlgorithmId},{"frames",frames},{"target",target},{"pitchMilliCents",pitch},{"formantPreserved",formant},{"channelPolicy",channels<=2?"mono-stereo-together":"discrete-apart"}};
        auto keyText=key.dump();media_io::SampleHash keyHash;keyHash.updateBytes({reinterpret_cast<const std::byte *>(keyText.data()),keyText.size()});auto renderKey=keyHash.digest();
        auto intent=key;intent["protocol"]=protocol;intent["operation"]=operation.str();intent["complete"]=false;intent["assetId"]=asset.id.str();intent["relative"]=asset.relativePath;intent["sourceFrames"]=asset.frames;media_io::publishJournal(job/"intent.json",intent.dump());
        std::cout<<Json({{"protocol",protocol},{"event","ready"},{"operation",operation.str()},{"renderKey",renderKey}}).dump()<<'\n'<<std::flush;
        // Parent acknowledges the prepared identity before study/process/drain.
        // Cancellation and the hard deadline remain active during this wait.
        while(!std::filesystem::exists(std::filesystem::symlink_status(job/"start.request"))){cancel();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        cancel();
        std::vector<float> interleaved(samples),planar(samples),output(samples),sourceWindow(sourceWindowSamples);std::vector<const float *> inputPointers(channels);std::vector<float *> outputPointers(channels);
        auto read=[&](std::uint64_t pos,std::size_t n){cancel();const auto range=sourceKernel.sourceRange(sourceMap,Frame(pos),std::uint32_t(n),asset.frames);cache.read(0,range.first,{sourceWindow.data(),range.frames*channels});sourceKernel.process(sourceMap,Frame(pos),asset.frames,range.first,{sourceWindow.data(),range.frames*channels},{interleaved.data(),n*channels});for(std::size_t ch=0;ch<channels;++ch){inputPointers[ch]=planar.data()+ch*512;for(std::size_t f=0;f<n;++f){auto x=interleaved[f*channels+ch];require(std::isfinite(x),"Nonfinite stretch source",ErrorCode::MediaMismatch);planar[ch*512+f]=x;}}};
        for(std::uint64_t pos=0;pos<frames;pos+=512){auto n=std::size_t(std::min<std::uint64_t>(512,frames-pos));read(pos,n);rb.study(inputPointers.data(),n,pos+n==frames);cancel();}
        Wave wave(job/"audio.partial",asset.sampleRate,channels);media_io::SampleHash sampleHash;std::uint64_t written=0;double peak=0;
        auto drain=[&]{while(rb.available()>0){cancel();auto n=std::min(512,rb.available());for(std::size_t ch=0;ch<channels;++ch)outputPointers[ch]=output.data()+ch*512;auto got=rb.retrieve(outputPointers.data(),std::size_t(n));require(got>0 && got<=std::size_t(n),"Stretch output stalled",ErrorCode::InvalidState);require(got<=target-written,"Stretch backend exceeded exact duration",ErrorCode::MediaMismatch);for(std::size_t f=0;f<got;++f)for(std::size_t ch=0;ch<channels;++ch){auto x=output[ch*512+f];require(std::isfinite(x),"Nonfinite stretch output",ErrorCode::MediaMismatch);interleaved[f*channels+ch]=x;peak=std::max(peak,std::abs(double(x)));}require(sf_writef_float(wave.file,interleaved.data(),sf_count_t(got))==sf_count_t(got) && sf_error(wave.file)==SF_ERR_NO_ERROR,"Stretch WAV write failed",ErrorCode::Io);sampleHash.update({interleaved.data(),got*channels});written+=got;}};
        for(std::uint64_t pos=0;pos<frames;pos+=512){auto n=std::size_t(std::min<std::uint64_t>(512,frames-pos));read(pos,n);rb.process(inputPointers.data(),n,pos+n==frames);drain();cancel();}
        drain();require(rb.available()==-1 && written==target,"Stretch did not drain to exact duration",ErrorCode::MediaMismatch);wave.close();cancel();
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
    }catch(const std::bad_alloc &){std::cerr<<"{\"protocol\":\"sc-stretch-render-v2\",\"complete\":false,\"messageId\":\"stretch.resource_limit\",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;
    }catch(const std::system_error &e){
        // Thread creation under the process ceiling can fail before the vendor
        // allocation, particularly with Debug binaries and larger runtime maps.
        // Preserve an actual resource refusal without treating every OS error
        // as exhaustion. Avoid constructing JSON while memory is constrained.
        const bool resource=e.code()==std::errc::resource_unavailable_try_again || e.code()==std::errc::not_enough_memory;
        std::cerr<<"{\"protocol\":\"sc-stretch-render-v2\",\"complete\":false,\"messageId\":\""<<(resource?"stretch.resource_limit":"stretch.render_failed")<<"\",\"systemErrorCode\":"<<e.code().value()<<",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;
    }catch(const ProjectError &e){std::cerr<<Json({{"protocol",protocol},{"complete",false},{"messageId","stretch.render_failed"},{"errorCode",unsigned(e.code())},{"publicationMayHaveCommitted",publicationMayHaveCommitted}}).dump()<<'\n';return 1;
    }catch(...){std::cerr<<"{\"protocol\":\"sc-stretch-render-v2\",\"complete\":false,\"messageId\":\"stretch.render_failed\",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){std::vector<std::string> args;for(int i=0;i<argc;++i){auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 1;std::string s(std::size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr))return 1;s.pop_back();args.push_back(std::move(s));}return run(args);}
#else
int main(int argc,char **argv){return run({argv,argv+argc});}
#endif
