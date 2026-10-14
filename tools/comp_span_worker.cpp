// SPDX-License-Identifier: GPL-3.0-only
// Disposable off-audio process. Process ceiling/watchdog and RF64 publication
// reuse the project's existing stretch-worker conventions; no privilege sandbox.
#include <soundcurrent/comp_span_protocol.hpp>
#include <soundcurrent/comp_span_history.hpp>
#include <soundcurrent/media_cache.hpp>
#include "media_io.hpp"
#include <sndfile.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif
using namespace soundcurrent::daw;
namespace {
using Json=nlohmann::json;
void check(bool v,const char *m,ErrorCode code=ErrorCode::InvalidParameter){if(!v)throw ProjectError(code,m);}
std::uint64_t decimal(const std::string &s){std::uint64_t v=0;auto r=std::from_chars(s.data(),s.data()+s.size(),v);check(r.ec==std::errc{} && r.ptr==s.data()+s.size(),"Invalid span worker trusted limit");return v;}
void processLimit(std::uint64_t bytes){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);auto job=CreateJobObjectW(nullptr,nullptr);check(job!=nullptr,"Cannot create span memory job",ErrorCode::ResourceLimit);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;info.ProcessMemoryLimit=SIZE_T(bytes);
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info)) || !AssignProcessToJobObject(job,GetCurrentProcess())){CloseHandle(job);throw ProjectError(ErrorCode::ResourceLimit,"Cannot enforce span memory ceiling");}
#else
    rlimit memory{rlim_t(bytes),rlim_t(bytes)};check(setrlimit(RLIMIT_AS,&memory)==0,"Cannot enforce span address-space ceiling",ErrorCode::ResourceLimit);
    rlimit core{0,0};check(setrlimit(RLIMIT_CORE,&core)==0,"Cannot disable span core dump",ErrorCode::ResourceLimit);
#endif
}
std::filesystem::path root(const std::string &s){check(s.size()<=4096 && validUtf8(s) && s.find('\0')==s.npos,"Invalid span worker root");auto p=utf8Path(s);check(p.is_absolute() && p==p.lexically_normal(),"Span worker root must be canonical absolute");auto at=p.root_path();media_io::plainDirectory(at);for(const auto &part:p.relative_path()){at/=part;media_io::plainDirectory(at);}return p;}
struct Wave {
    media_io::File descriptor;SNDFILE *file=nullptr;
    Wave(const std::filesystem::path &p,unsigned rate,unsigned channels):descriptor(p,true){SF_INFO info{};info.samplerate=int(rate);info.channels=int(channels);info.format=SF_FORMAT_RF64|SF_FORMAT_FLOAT;file=sf_open_fd(descriptor.descriptor(),SFM_WRITE,&info,SF_FALSE);check(file!=nullptr,"Cannot create span RF64",ErrorCode::Io);}
    ~Wave(){if(file)sf_close(file);}
    void close(){auto f=file;file=nullptr;check(sf_close(f)==0,"Cannot finalize span RF64",ErrorCode::Io);descriptor.flush();descriptor.close();}
};
int run(const std::vector<std::string> &args){bool publicationMayHaveCommitted=false;std::string_view protocol=compSpanProtocol;
    try{
        check(args.size()==15 && args[1]=="--project-root" && args[3]=="--jobs-root" && args[5]=="--memory-mib" && args[7]=="--maximum-source-bytes" && args[9]=="--maximum-output-bytes" && args[11]=="--deadline-ms" && args[13]=="--request-sha256","Invalid span worker arguments");
        CompSpanWorkerPolicy policy;const auto mib=decimal(args[6]);check(mib>=128 && mib<=4096,"Invalid span memory grant");policy.memoryBytes=mib*1024*1024;policy.maximumSourceBytes=decimal(args[8]);policy.maximumOutputBytes=decimal(args[10]);policy.deadlineMilliseconds=decimal(args[12]);
        check(policy.memoryBytes>=128ULL*1024*1024 && policy.memoryBytes<=4096ULL*1024*1024 && policy.maximumSourceBytes>0 && policy.maximumSourceBytes<=8ULL*1024*1024*1024 && policy.maximumOutputBytes>=4096 && policy.maximumOutputBytes<=8ULL*1024*1024*1024 && policy.deadlineMilliseconds>=100 && policy.deadlineMilliseconds<=3600000,"Invalid span worker policy");
        processLimit(policy.memoryBytes);std::thread([milliseconds=policy.deadlineMilliseconds]{std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));std::_Exit(2);}).detach();
        ResourceLedger ledger(std::size_t(policy.memoryBytes),"Span worker owned payload");auto bank=ledger.reserve(compSpanRequestMaximum*2);std::string bytes;bytes.reserve(compSpanRequestMaximum);char c;
        while(std::cin.get(c)){check(bytes.size()<compSpanRequestMaximum,"Span request too large",ErrorCode::ResourceLimit);bytes.push_back(c);}
        const auto executable=media_io::executingImageSha256();
        auto request=decodeCompSpanRequest(bytes,args[14],{1,"owned-span-worker"},executable,ledger);protocol=request.protocol();bank.resize(0);bytes.clear();bytes.shrink_to_fit();
        auto sourceRoot=root(args[2]),jobsRoot=root(args[4]);check(jobsRoot==sourceRoot/"media"/"comp-spans","Span job root is outside its owned project");
        const auto job=jobsRoot/request.operation().str();const auto poll=[&]{if(std::filesystem::exists(std::filesystem::symlink_status(job/"cancel.request")))throw ProjectError(ErrorCode::Canceled,"Span canceled before commit");};
        admitCompSpanSources(sourceRoot,request,policy,ledger,poll);
        verifyCompSpanHistory(sourceRoot,request,policy,ledger,poll);
        const auto intentText=compSpanIntent(request,policy,ledger);const auto intent=Json::parse(intentText);
        check(std::filesystem::create_directory(job),"Span operation already exists",ErrorCode::Io);media_io::flushDirectory(jobsRoot);media_io::JobLease writer(job,true);
        check(writer.status()==media_io::LeaseStatus::Held,"Span writer ownership unavailable",ErrorCode::Io);media_io::publishJournal(job/"intent.json",intentText);
        auto ready=intent;ready["event"]="ready";std::cout<<ready.dump()<<'\n'<<std::flush;
        while(!std::filesystem::exists(std::filesystem::symlink_status(job/"start.request"))){poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}poll();
        const auto &asset=request.span().source();MediaCacheConfig config;config.maximumOpenFiles=1;config.pageFrames=256;config.cacheBudgetBytes=std::size_t(256)*asset.layout.channels*sizeof(float)+256;config.registryBudgetBytes=256*1024;config.resources=ledger;
        MediaReadCache cache(sourceRoot,std::span<const Asset>(&asset,1),asset.sampleRate,config,poll);Wave wave(job/"audio.partial",request.span().outputSampleRate(),asset.layout.channels);media_io::SampleHash samples;
        const auto rendered=renderCompSpan(request.span(),[&](Frame at,std::span<float> values){poll();cache.read(0,at,values);},[&](std::span<const float> values){poll();const auto frames=values.size()/asset.layout.channels;
            check(sf_writef_float(wave.file,values.data(),sf_count_t(frames))==sf_count_t(frames) && sf_error(wave.file)==SF_ERR_NO_ERROR,"Span RF64 write failed",ErrorCode::Io);samples.update(values);},ledger,poll);
        wave.close();poll();check(std::filesystem::file_size(job/"audio.partial")<=policy.maximumOutputBytes,"Span file exceeds grant",ErrorCode::ResourceLimit);
        const auto audioSha=hashMediaFile(job/"audio.partial",poll);admitCompSpanSources(sourceRoot,request,policy,ledger,poll);check(media_io::executingImageSha256(poll)==executable,"Span binary changed during processing",ErrorCode::MediaMismatch);poll();
        verifyCompSpanHistory(sourceRoot,request,policy,ledger,poll);
        auto complete=intent;complete["complete"]=true;complete["writtenFrames"]=rendered.writtenFrames;complete["audioSha256"]=audioSha;complete["sampleSha256"]=samples.digest();complete["peakLinear"]=rendered.floatPeak;complete["payloadPeakBytes"]=ledger.usage().peakBytes;
        complete["durabilityMinimum"]="file-flushed";complete["memoryMetric"]=
#ifdef _WIN32
            "process-commit";
#else
            "address-space";
#endif
        media_io::publishMedia(job/"audio.partial",job/"audio.wav");poll();const auto completeText=complete.dump();media_io::File marker(job/"complete.partial",true);marker.write(completeText);marker.flush();marker.close();poll();publicationMayHaveCommitted=true;media_io::publishMedia(job/"complete.partial",job/"complete.json");std::cout<<completeText<<'\n';return std::cout?0:1;
    }catch(const ProjectError &e){std::cerr<<Json{{"protocol",protocol},{"complete",false},{"errorCode",unsigned(e.code())},{"publicationMayHaveCommitted",publicationMayHaveCommitted}}.dump()<<'\n';return 1;
    }catch(...){std::cerr<<"{\"protocol\":\""<<protocol<<"\",\"complete\":false,\"messageId\":\"comp_span.worker_failed\",\"publicationMayHaveCommitted\":"<<(publicationMayHaveCommitted?"true":"false")<<"}\n";return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){std::vector<std::string> args;for(int i=0;i<argc;++i){const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 1;std::string s(std::size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr))return 1;s.pop_back();args.push_back(std::move(s));}return run(args);}
#else
int main(int argc,char **argv){return run({argv,argv+argc});}
#endif
