// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wave_validation.hpp>
#include <sndfile.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <climits>
#include <cstdio>
#include <exception>
#include <limits>
#include <vector>

namespace soundcurrent::daw {
namespace {
void require(bool ok,const char *text,ErrorCode code=ErrorCode::InvalidParameter) {
    if (!ok) throw ProjectError(code,text);
}
struct Io {
    ApprovedMediaFile &file;
    WaveValidationLimits limits;
    std::stop_token stop;
    const std::function<void()> &beforeRead;
    std::uint64_t position=0,bytes=0,operations=0;
    std::exception_ptr failure;
    Io(ApprovedMediaFile &owned,WaveValidationLimits policy,std::stop_token token,
       const std::function<void()> &callback)
        :file(owned),limits(policy),stop(token),beforeRead(callback) {}
    void poll() const {
        require(!stop.stop_requested(),"Wave validation canceled",ErrorCode::Canceled);
        if (beforeRead) beforeRead();
    }
    void account(std::uint64_t count=0) {
        poll();require(operations<limits.maximumIoOperations,"Wave validation I/O operation limit exceeded",ErrorCode::ResourceLimit);
        require(count<=limits.maximumBytesRead-bytes,"Wave validation read-byte limit exceeded",ErrorCode::ResourceLimit);
        ++operations;bytes+=count;
    }
    void read(std::uint64_t offset,std::span<char> target) {
        account(target.size());file.readAt(offset,target,stop);
    }
    template<class F> sf_count_t boundary(sf_count_t refusal,F fn) noexcept {
        if (failure) return refusal;
        try { account();return fn(); }
        catch (...) { failure=std::current_exception();return refusal; }
    }
    void rethrow() const { if (failure) std::rethrow_exception(failure); }
};
sf_count_t fileLength(void *raw) noexcept {
    auto &io=*static_cast<Io *>(raw);return io.boundary(-1,[&]{return static_cast<sf_count_t>(io.file.size());});
}
sf_count_t tell(void *raw) noexcept {
    auto &io=*static_cast<Io *>(raw);return io.boundary(-1,[&]{return static_cast<sf_count_t>(io.position);});
}
sf_count_t seek(sf_count_t offset,int whence,void *raw) noexcept {
    auto &io=*static_cast<Io *>(raw);
    return io.boundary(-1,[&] {
        const auto size=io.file.size();std::uint64_t base=0;
        if (whence==SEEK_CUR) base=io.position;
        else if (whence==SEEK_END) base=size;
        else require(whence==SEEK_SET,"Invalid decoder seek origin");
        if (offset<0) {
            // Avoid negating the minimum signed value.
            const auto magnitude=static_cast<std::uint64_t>(-(offset+1))+1;
            require(magnitude<=base,"Decoder seek before approved file");base-=magnitude;
        } else {
            require(static_cast<std::uint64_t>(offset)<=size-base,"Decoder seek beyond approved file");
            base+=static_cast<std::uint64_t>(offset);
        }
        io.position=base;return static_cast<sf_count_t>(base);
    });
}
sf_count_t read(void *buffer,sf_count_t requested,void *raw) noexcept {
    auto &io=*static_cast<Io *>(raw);
    return io.boundary(0,[&] {
        require(requested>=0 && (requested==0 || buffer),"Invalid decoder read request");
        const auto count=std::min<std::uint64_t>(static_cast<std::uint64_t>(requested),io.file.size()-io.position);
        std::uint64_t used=0;
        while (used<count) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(65536,count-used));
            io.read(io.position,std::span(static_cast<char *>(buffer)+static_cast<std::size_t>(used),n));
            io.position+=n;used+=n;
        }
        return static_cast<sf_count_t>(used);
    });
}
sf_count_t write(const void *,sf_count_t,void *raw) noexcept {
    auto &io=*static_cast<Io *>(raw);return io.boundary(0,[]()->sf_count_t {
        throw ProjectError(ErrorCode::InvalidState,"Read-only approved media decoder attempted writing");
    });
}
std::uint64_t integer(std::span<const char> bytes,bool bigEndian) {
    std::uint64_t result=0;
    for (std::size_t i=0;i<bytes.size();++i) {
        const auto index=bigEndian ? i : bytes.size()-1-i;
        result=(result<<8)|static_cast<unsigned char>(bytes[index]);
    }
    return result;
}
struct Header {
    WaveValidation result;
    std::uint64_t dataBytes=0,dataOffset=0;
    int subtype=0;
    bool rf64=false;
};
Header inspect(Io &io) {
    Header h;h.result.sourceBytes=io.file.size();
    require(h.result.sourceBytes>=12,"Wave container header is truncated");
    std::array<char,12> header{};io.read(0,header);
    const std::string_view magic(header.data(),4);
    require(magic=="RIFF" || magic=="RIFX" || magic=="RF64","Wave container not yet supported",ErrorCode::UnsupportedSchema);
    require(std::string_view(header.data()+8,4)=="WAVE","RIFF type is not WAVE",ErrorCode::UnsupportedSchema);
    h.result.bigEndian=magic=="RIFX";
    const auto declared=integer(std::span(header).subspan(4,4),h.result.bigEndian);
    h.rf64=magic=="RF64";
    require((h.rf64 && declared==UINT32_MAX) || declared==h.result.sourceBytes-8,"Wave container extent does not match approved file");
    struct SizeEntry {std::array<char,4> tag;std::uint64_t bytes;bool used=false;};
    std::vector<SizeEntry> sizes;ResourceLease tableGrant;bool haveSizes=false;
    std::uint64_t dataSize64=0,samples64=0;
    bool haveFormat=false,haveData=false;std::uint32_t chunkCount=0;std::uint16_t blockAlign=0;
    for (std::uint64_t offset=12;offset<h.result.sourceBytes;) {
        require(chunkCount++<io.limits.maximumChunks,"Wave chunk limit exceeded",ErrorCode::ResourceLimit);
        require(h.result.sourceBytes-offset>=8,"Wave chunk header is truncated");
        std::array<char,8> chunk{};io.read(offset,chunk);offset+=8;
        auto length=integer(std::span(chunk).subspan(4,4),h.result.bigEndian);
        const std::string_view tag(chunk.data(),4);
        if(h.rf64 && chunkCount==1) require(tag=="ds64","RF64 size chunk must be first");
        if(h.rf64 && length==UINT32_MAX) {
            require(haveSizes,"RF64 sentinel precedes size table");
            if(tag=="data") length=dataSize64;
            else {
                auto found=std::find_if(sizes.begin(),sizes.end(),[&](const SizeEntry &s){return !s.used && std::string_view(s.tag.data(),4)==tag;});
                require(found!=sizes.end(),"RF64 sentinel has no size table entry");found->used=true;length=found->bytes;
            }
        }
        require(length<=UINT64_MAX-(length&1),"Wave padded chunk length overflow");
        const auto padded=length+(length&1);
        require(padded<=h.result.sourceBytes-offset,"Wave chunk exceeds approved container");
        if(tag=="ds64" && h.rf64) {
            require(!haveSizes && chunkCount==1 && length>=28,"Invalid/duplicate RF64 size chunk");
            std::array<char,28> sizeHeader{};io.read(offset,sizeHeader);
            const auto value=[&](std::size_t from,std::size_t n){return integer(std::span(sizeHeader).subspan(from,n),false);};
            const auto riff=value(0,8);dataSize64=value(8,8);samples64=value(16,8);const auto count=value(24,4);
            require(declared!=UINT32_MAX || riff==h.result.sourceBytes-8,"RF64 container extent differs from approved file");
            require(count<=io.limits.maximumChunks && count<=SIZE_MAX/sizeof(SizeEntry),"RF64 table exceeds chunk admission",ErrorCode::ResourceLimit);
            require(28+count*12<=length,"Truncated RF64 size table");
            tableGrant=io.file.resourceLedger().reserve(std::size_t(count)*sizeof(SizeEntry));sizes.reserve(std::size_t(count));
            for(std::uint64_t i=0;i<count;++i) {
                std::array<char,12> entry{};io.read(offset+28+i*12,entry);SizeEntry size{};std::copy_n(entry.begin(),4,size.tag.begin());size.bytes=integer(std::span(entry).subspan(4,8),false);sizes.push_back(size);
            }
            haveSizes=true;
        } else if (tag=="fmt ") {
            require(!haveFormat && !haveData && length>=16,"Duplicate/late/truncated wave format");
            std::array<char,40> fmt{};
            io.read(offset,std::span(fmt).first(static_cast<std::size_t>(std::min<std::uint64_t>(length,fmt.size()))));
            const auto value=[&](std::size_t from,std::size_t n){return integer(std::span(fmt).subspan(from,n),h.result.bigEndian);};
            auto format=value(0,2);h.result.channels=static_cast<std::uint32_t>(value(2,2));
            h.result.rate=static_cast<std::uint32_t>(value(4,4));blockAlign=static_cast<std::uint16_t>(value(12,2));
            h.result.bitsPerSample=static_cast<std::uint32_t>(value(14,2));
            require(h.result.channels>0 && h.result.rate>0 && h.result.rate<=static_cast<std::uint32_t>(INT_MAX),"Invalid wave rate/channel count");
            require(h.result.channels<=io.limits.maximumChannels && h.result.channels<=1024,"Wave channel admission limit exceeded",ErrorCode::ResourceLimit);
            if (format==0xfffe) {
                require(!h.result.bigEndian,"Big-endian extensible WAVE not yet supported",ErrorCode::UnsupportedSchema);
                require(length>=40 && value(16,2)>=22 && value(16,2)<=length-18,"Invalid extensible WAVE format");
                require(value(18,2)==h.result.bitsPerSample,"Partial-precision extensible WAVE not yet supported",ErrorCode::UnsupportedSchema);
                // Original subtype GUID, not vendor code or a path to a codec.
                constexpr std::array<unsigned char,12> suffix{0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                for (std::size_t i=0;i<suffix.size();++i)
                    require(static_cast<unsigned char>(fmt[28+i])==suffix[i],"Extensible WAVE subtype not yet supported",ErrorCode::UnsupportedSchema);
                format=value(24,4);h.result.extensible=true;h.result.channelMask=static_cast<std::uint32_t>(value(20,4));
                require(!h.result.channelMask || std::popcount(h.result.channelMask)==static_cast<int>(h.result.channels),"Wave channel mask/count mismatch");
            }
            const auto bits=h.result.bitsPerSample;
            if (format==1) {
                if (bits==8) {h.result.encoding=WaveEncoding::Unsigned8;h.subtype=SF_FORMAT_PCM_U8;}
                else if (bits==16) {h.result.encoding=WaveEncoding::Signed16;h.subtype=SF_FORMAT_PCM_16;}
                else if (bits==24) {h.result.encoding=WaveEncoding::Signed24;h.subtype=SF_FORMAT_PCM_24;}
                else if (bits==32) {h.result.encoding=WaveEncoding::Signed32;h.subtype=SF_FORMAT_PCM_32;}
                else require(false,"PCM precision not yet supported",ErrorCode::UnsupportedSchema);
            } else if (format==3 && (bits==32 || bits==64)) {
                h.result.encoding=bits==32 ? WaveEncoding::Float32 : WaveEncoding::Float64;
                h.subtype=bits==32 ? SF_FORMAT_FLOAT : SF_FORMAT_DOUBLE;
            } else require(false,"Wave codec not yet supported",ErrorCode::UnsupportedSchema);
            require(blockAlign==h.result.channels*(bits/8) && value(8,4)==std::uint64_t(h.result.rate)*blockAlign,"Wave byte rate/alignment mismatch");
            haveFormat=true;
        } else if (tag=="data") {
            require(haveFormat && !haveData,"Missing format or multiple wave data chunks");
            h.dataBytes=length;h.dataOffset=offset;haveData=true;
        }
        offset+=padded;
    }
    require(haveFormat && haveData && blockAlign && h.dataBytes%blockAlign==0,"Wave format/data extent incomplete");
    require(!h.rf64 || (haveSizes && std::all_of(sizes.begin(),sizes.end(),[](const auto &s){return s.used;})),"Incomplete/unused RF64 size table");
    h.result.frames=h.dataBytes/blockAlign;
    require(!h.rf64 || !samples64 || samples64==h.result.frames,"RF64 sample count differs from data extent");
    require(h.result.frames<=io.limits.maximumFrames,"Wave frame admission limit exceeded",ErrorCode::ResourceLimit);
    return h;
}
struct Decoder {
    SNDFILE *handle=nullptr;
    Decoder()=default;
    Decoder(const Decoder &)=delete;
    Decoder &operator=(const Decoder &)=delete;
    ~Decoder() { if (handle) sf_close(handle); }
};
} // namespace
WaveValidation validateApprovedWave(ApprovedMediaFile &file,WaveValidationLimits limits,
    std::stop_token stop,const std::function<void(std::uint64_t,std::span<const double>)> &observer,
    const std::function<void()> &beforeRead) {
    if (beforeRead) beforeRead();
    require(limits.maximumFrames && limits.maximumBytesRead && limits.maximumIoOperations && limits.maximumChunks &&
        limits.maximumChannels && limits.maximumChannels<=1024 && limits.blockFrames && limits.blockFrames<=8192,"Invalid wave validation policy");
    require(file.size()<=static_cast<std::uint64_t>(std::numeric_limits<sf_count_t>::max()),"Approved file exceeds decoder address range",ErrorCode::ResourceLimit);
    require(!stop.stop_requested(),"Wave validation canceled",ErrorCode::Canceled);file.verifyUnchanged();
    // Admitted work allowance is not an OS memory limit for third-party parsing.
    auto decoderWork=file.resourceLedger().reserve(256*1024);
    Io io{file,limits,stop,beforeRead};
    auto header=inspect(io);auto result=header.result;
    const auto samples=std::size_t(limits.blockFrames)*result.channels;
    auto blockWork=file.resourceLedger().reserve(samples*sizeof(double));std::vector<double> buffer(samples);
    if(header.rf64) {
        // RF64 chunk extents are already independently validated. Decode the
        // admitted PCM/IEEE bytes directly: libsndfile 1.2.2 mishandles odd
        // ancillary RF64 chunks. No altered header or source file is exposed.
        const auto width=result.bitsPerSample/8;
        const auto rawBytes=samples*width;
        auto rawGrant=file.resourceLedger().reserve(rawBytes);std::vector<char> raw(rawBytes);
        while(result.decodedFrames<result.frames) {
            io.poll();const auto frames=std::min<std::uint64_t>(limits.blockFrames,result.frames-result.decodedFrames);
            const auto count=std::size_t(frames)*result.channels;
            const auto size=count*width,offset=header.dataOffset+result.decodedFrames*result.channels*width;
            for(std::size_t at=0;at<size;) {
                const auto n=std::min<std::size_t>(65536,size-at);io.read(offset+at,{raw.data()+at,n});at+=n;
            }
            for(std::size_t i=0;i<count;++i) {
                const auto value=integer({raw.data()+i*width,width},false);double sample;
                if(result.encoding==WaveEncoding::Float32) sample=double(std::bit_cast<float>(std::uint32_t(value)));
                else if(result.encoding==WaveEncoding::Float64) sample=std::bit_cast<double>(value);
                else if(result.encoding==WaveEncoding::Unsigned8) sample=(double(value)-128)/128;
                else {
                    const auto sign=std::uint64_t(1)<<(result.bitsPerSample-1);
                    const auto signedValue=value<sign?std::int64_t(value):std::int64_t(value)-std::int64_t(sign*2);
                    sample=double(signedValue)/std::ldexp(1.0,int(result.bitsPerSample)-1);
                }
                require(std::isfinite(sample),"Nonfinite RF64 sample");buffer[i]=sample;result.peak=std::max(result.peak,std::abs(sample));
            }
            if(observer) observer(result.decodedFrames,{buffer.data(),count});
            result.decodedFrames+=frames;
        }
    } else {
    SF_VIRTUAL_IO callbacks{fileLength,seek,read,write,tell};SF_INFO info{};
    Decoder decoder;decoder.handle=sf_open_virtual(&callbacks,SFM_READ,&info,&io);io.rethrow();
    require(decoder.handle!=nullptr,"Approved WAVE decoder refused source");
    const auto major=info.format&SF_FORMAT_TYPEMASK;
    require((header.rf64 ? major==SF_FORMAT_RF64 : (major==SF_FORMAT_WAV || major==SF_FORMAT_WAVEX)) && (info.format&SF_FORMAT_SUBMASK)==header.subtype &&
        info.frames>=0 && static_cast<std::uint64_t>(info.frames)==result.frames && info.channels==static_cast<int>(result.channels) &&
        info.samplerate==static_cast<int>(result.rate),"Decoder metadata differs from validated WAVE header");
    sf_command(decoder.handle,SFC_SET_NORM_DOUBLE,nullptr,SF_TRUE);io.rethrow();
    while (result.decodedFrames<result.frames) {
        io.poll();const auto wanted=std::min<std::uint64_t>(limits.blockFrames,result.frames-result.decodedFrames);
        const auto got=sf_readf_double(decoder.handle,buffer.data(),static_cast<sf_count_t>(wanted));io.rethrow();
        require(got==static_cast<sf_count_t>(wanted) && sf_error(decoder.handle)==SF_ERR_NO_ERROR,"Wave decoder stopped before admitted frame extent");
        const auto block=std::span(buffer).first(static_cast<std::size_t>(got)*result.channels);
        for (const auto sample:block) {require(std::isfinite(sample),"Wave source contains non-finite samples");result.peak=std::max(result.peak,std::abs(sample));}
        if (observer) observer(result.decodedFrames,block);
        io.poll();result.decodedFrames+=static_cast<std::uint64_t>(got);
    }
    const auto extra=sf_readf_double(decoder.handle,buffer.data(),1);io.rethrow();
    require(extra==0,"Wave decoder produced frames beyond admitted extent");
    require(sf_error(decoder.handle)==SF_ERR_NO_ERROR,"Wave decoder reported a content error");
    const auto close=sf_close(decoder.handle);decoder.handle=nullptr;io.rethrow();require(close==0,"Wave decoder close failed",ErrorCode::Io);
    }
    std::uint64_t hashOffset=0;
    result.sourceSha256=file.digest(stop,[&] {
        const auto count=std::min<std::uint64_t>(65536,file.size()-hashOffset);io.account(count);hashOffset+=count;
    });
    file.verifyUnchanged();io.poll();result.bytesRead=io.bytes;result.ioOperations=io.operations;return result;
}
} // namespace soundcurrent::daw
