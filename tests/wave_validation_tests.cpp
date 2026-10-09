// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wave_validation.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool good,const char *text) {++checks;if (!good) throw std::runtime_error(text);}
template<class F> void refused(F fn,ErrorCode code) {
    bool seen=false;try {fn();} catch (const ProjectError &e) {seen=true;check(e.code()==code,"Unexpected WAVE refusal code");}
    check(seen,"WAVE refusal missing");
}
void put(std::string &bytes,std::uint64_t value,unsigned count,bool big=false) {
    for (unsigned i=0;i<count;++i) {
        const auto shift=8*(big ? count-1-i : i);bytes.push_back(static_cast<char>((value>>shift)&255));
    }
}
void set(std::string &bytes,std::size_t offset,std::uint64_t value,unsigned count,bool big=false) {
    std::string replacement;put(replacement,value,count,big);bytes.replace(offset,count,replacement);
}
void chunk(std::string &body,std::string_view tag,std::string_view data,bool big=false) {
    body+=tag;put(body,data.size(),4,big);body+=data;if (data.size()&1) body+='\0';
}
std::string wave(unsigned bits,unsigned channels,std::string_view data,bool floating=false,bool big=false,
                 bool extensible=false,std::uint32_t mask=0,unsigned extraChunks=0,unsigned rate=48000) {
    std::string fmt;put(fmt,extensible ? 0xfffe : floating ? 3 : 1,2,big);put(fmt,channels,2,big);put(fmt,rate,4,big);
    const auto alignment=channels*(bits/8);put(fmt,std::uint64_t(rate)*alignment,4,big);put(fmt,alignment,2,big);put(fmt,bits,2,big);
    if (extensible) {
        put(fmt,22,2);put(fmt,bits,2);put(fmt,mask,4);put(fmt,floating ? 3 : 1,4);
        for (const unsigned c:{0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71}) fmt.push_back(static_cast<char>(c));
    }
    std::string body="WAVE";chunk(body,"fmt ",fmt,big);
    for (unsigned i=0;i<extraChunks;++i) chunk(body,"JUNK","odd",big);
    chunk(body,"data",data,big);std::string out=big ? "RIFX" : "RIFF";put(out,body.size(),4,big);return out+body;
}
void write(const std::filesystem::path &path,std::string_view data) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(data.data(),static_cast<std::streamsize>(data.size()));out.close();
    check(bool(out),"Cannot write owned WAVE fixture");
}
void usage(const ResourceLedger &memory,ResourceUsage before) {
    const auto now=memory.usage();check(now.owners==before.owners && now.reservedBytes==before.reservedBytes,"WAVE work lease leaked");
}
void sampleCase(const std::filesystem::path &path,ResourceLedger memory,unsigned bits,bool floating,bool big,bool ext,unsigned channels) {
    std::vector<double> expected;std::string data;constexpr std::array<std::int32_t,7> integers{-128,0,63,-57,127,-1,1};
    for (unsigned frame=0;frame<19;++frame) for (unsigned channel=0;channel<channels;++channel) {
        const auto index=(frame+3*channel)%integers.size();const auto small=integers[index];
        if (floating) {
            const auto ordinary=static_cast<double>(small)/32;
            // Check precision beyond float32 for float64; use the exact stored
            // float32 neighbor as its independent expected representation.
            const auto value=index==6 ? (bits==32 ? static_cast<double>(std::nextafter(0.03125F,1.0F)) :
                std::nextafter(0.03125,1.0)) : ordinary;
            expected.push_back(value);
            if (bits==32) put(data,std::bit_cast<std::uint32_t>(static_cast<float>(value)),4,big);
            else put(data,std::bit_cast<std::uint64_t>(value),8,big);
        } else {
            const auto scale=std::int64_t{1}<<(bits-8);
            // Lowest-bit positive/negative samples catch premature reduction
            // of PCM24/32 to PCM16 rather than just testing the high byte.
            const auto value=static_cast<std::int64_t>(small)*(index>=5 ? 1 : scale);
            expected.push_back(static_cast<double>(value)/std::ldexp(1.0,static_cast<int>(bits)-1));
            put(data,static_cast<std::uint64_t>(bits==8 ? value+128 : value),bits/8,big);
        }
    }
    const auto mask=ext && channels==2 ? 3U : 0U;const auto bytes=wave(bits,channels,data,floating,big,ext,mask,3,12345);write(path/"samples.wav",bytes);
    ApprovedMediaRoot root(path,memory);auto file=root.open("samples.wav",bytes.size());const auto baseline=memory.usage();
    WaveValidationLimits limits;limits.blockFrames=3;std::size_t seen=0;
    const auto result=validateApprovedWave(file,limits,{},[&](std::uint64_t first,std::span<const double> block) {
        check(first*channels==seen,"WAVE observer frame position changed");
        for (const auto value:block) {check(seen<expected.size() && value==expected[seen],"Independent PCM/float sample oracle differs");++seen;}
    });
    check(seen==expected.size() && result.decodedFrames==19 && result.frames==19,"WAVE content not completely decoded");
    check(result.channels==channels && result.rate==12345 && result.bitsPerSample==bits,"Original WAVE metadata lost");
    check(result.bigEndian==big && result.extensible==ext && result.channelMask==mask,"WAVE layout/container metadata lost");
    double peak=0;for (const auto x:expected) peak=std::max(peak,std::abs(x));check(result.peak==peak,"Floating headroom/sample peak changed");
    check(result.sourceBytes==bytes.size() && result.bytesRead>=bytes.size()*2 && result.ioOperations>0,"WAVE work counters/byte extent wrong");
    const auto digest=file.digest();check(result.sourceSha256==digest,"WAVE original-byte digest changed");usage(memory,baseline);
}
}
int main() {
    const auto path=std::filesystem::temp_directory_path()/("sc-wave-owned-"+Id::generate().str());
    try {
        std::filesystem::create_directory(path);ResourceLedger memory(16*1024*1024,"Owned WAVE acceptance");
        for (const auto bits:{8U,16U,24U,32U}) for (const auto big:{false,true}) sampleCase(path,memory,bits,false,big,false,2);
        for (const auto bits:{32U,64U}) for (const auto big:{false,true}) sampleCase(path,memory,bits,true,big,false,2);
        sampleCase(path,memory,24,false,false,true,2);sampleCase(path,memory,32,true,false,true,2);
        sampleCase(path,memory,16,false,false,true,8);sampleCase(path,memory,16,false,false,false,1024);
        const auto valid=wave(16,1,std::string(1024,'\0'));write(path/"valid.wav",valid);
        {
            ApprovedMediaRoot root(path,memory);auto file=root.open("valid.wav",valid.size());const auto before=memory.usage();
            WaveValidationLimits limits;limits.maximumFrames=511;
            refused([&]{validateApprovedWave(file,limits);},ErrorCode::ResourceLimit);usage(memory,before);
            limits={};limits.maximumBytesRead=30;refused([&]{validateApprovedWave(file,limits);},ErrorCode::ResourceLimit);usage(memory,before);
            limits={};limits.maximumBytesRead=52;refused([&]{validateApprovedWave(file,limits);},ErrorCode::ResourceLimit);usage(memory,before);
            // Exhaust the virtual-I/O budget during sf_open_virtual, not the preflight.
            limits={};limits.maximumIoOperations=4;refused([&]{validateApprovedWave(file,limits);},ErrorCode::ResourceLimit);usage(memory,before);
            limits={};limits.blockFrames=0;refused([&]{validateApprovedWave(file,limits);},ErrorCode::InvalidParameter);usage(memory,before);
            std::stop_source stop;unsigned blocks=0;
            refused([&]{validateApprovedWave(file,{},stop.get_token(),[&](auto,std::span<const double>){++blocks;stop.request_stop();});},ErrorCode::Canceled);
            check(blocks==1,"WAVE cancellation failed at decoded block boundary");usage(memory,before);
            stop={};stop.request_stop();refused([&]{validateApprovedWave(file,{},stop.get_token());},ErrorCode::Canceled);usage(memory,before);
            memory.configure(before.reservedBytes);refused([&]{validateApprovedWave(file);},ErrorCode::ResourceLimit);usage(memory,before);memory.configure(16*1024*1024);
            bool observerThrew=false;
            try {validateApprovedWave(file,{}, {},[](auto,std::span<const double>){throw std::logic_error("Owned observer failure");});}
            catch (const std::logic_error &) {observerThrew=true;}
            check(observerThrew,"Observer failure lost");usage(memory,before);
            auto moved=std::move(file);refused([&]{validateApprovedWave(file);},ErrorCode::InvalidState);
            check(validateApprovedWave(moved).frames==512,"Failed checks damaged pinned file");
        }
        check(memory.usage().owners==0,"WAVE/file/root did not retire");
        auto bad=[&](std::string bytes,ErrorCode code,WaveValidationLimits limits=WaveValidationLimits{}) {
            write(path/"bad.wav",bytes);ApprovedMediaRoot root(path,memory);auto file=root.open("bad.wav",65536);const auto before=memory.usage();
            refused([&]{validateApprovedWave(file,limits);},code);usage(memory,before);
        };
        bad("RIFF",ErrorCode::InvalidParameter);
        auto corrupt=valid;corrupt.pop_back();bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=valid;set(corrupt,40,1026,4);bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=valid;set(corrupt,22,0,2);bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=valid;set(corrupt,24,0,4);bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=valid;set(corrupt,28,17,4);bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=valid;set(corrupt,32,7,2);bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=valid;set(corrupt,20,17,2);bad(corrupt,ErrorCode::UnsupportedSchema);
        corrupt=valid;set(corrupt,34,20,2);bad(corrupt,ErrorCode::UnsupportedSchema);
        corrupt=valid;corrupt.replace(0,4,"RF64");bad(corrupt,ErrorCode::UnsupportedSchema);
        corrupt=valid;corrupt.replace(8,4,"AVI ");bad(corrupt,ErrorCode::UnsupportedSchema);
        bad(wave(16,1,"x"),ErrorCode::InvalidParameter);
        corrupt=valid;chunk(corrupt,"data","");set(corrupt,4,corrupt.size()-8,4);bad(corrupt,ErrorCode::InvalidParameter);
        auto ext=wave(24,2,std::string(12,'\0'),false,false,true,3);
        corrupt=ext;set(corrupt,38,20,2);bad(corrupt,ErrorCode::UnsupportedSchema);
        corrupt=ext;set(corrupt,40,1,4);bad(corrupt,ErrorCode::InvalidParameter);
        corrupt=ext;corrupt[59]='x';bad(corrupt,ErrorCode::UnsupportedSchema);
        WaveValidationLimits limit;limit.maximumChunks=2;bad(wave(16,1,"",false,false,false,0,3),ErrorCode::ResourceLimit,limit);
        limit={};limit.maximumChannels=1;bad(ext,ErrorCode::ResourceLimit,limit);
        for (const auto bits:{32U,64U}) for (const auto value:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            std::string data;
            if (bits==32) put(data,std::bit_cast<std::uint32_t>(static_cast<float>(value)),4);
            else put(data,std::bit_cast<std::uint64_t>(value),8);
            bad(wave(bits,1,data,true),ErrorCode::InvalidParameter);
        }
        write(path/"empty.wav",wave(16,1,""));
        {ApprovedMediaRoot root(path,memory);auto file=root.open("empty.wav",100);const auto report=validateApprovedWave(file);check(report.frames==0 && report.decodedFrames==0 && report.peak==0,"Empty WAVE mishandled");}
#ifndef _WIN32
        write(path/"changing.wav",valid);
        {ApprovedMediaRoot root(path,memory);auto file=root.open("changing.wav",valid.size());const auto before=memory.usage();
         refused([&]{validateApprovedWave(file,{}, {},[&](auto,std::span<const double>){write(path/"changing.wav","changed");});},ErrorCode::MediaMismatch);usage(memory,before);}
#endif
        check(memory.usage().owners==0 && memory.usage().reservedBytes==0,"WAVE acceptance leaked resource credits");
        std::filesystem::remove_all(path);std::cout<<"PASS: "<<checks<<" pinned WAVE checks; independent PCM/float samples, headroom, full bounded content; no project conversion\n";return 0;
    } catch (const std::exception &e) {std::cerr<<e.what()<<"; retained owned fixture="<<path<<'\n';return 1;}
}
