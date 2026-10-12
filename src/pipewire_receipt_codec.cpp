// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_receipt_codec.hpp>
#include <bit>
namespace soundcurrent::daw {
namespace {
void require(bool b){if(!b)throw ProjectError(ErrorCode::InvalidState,"Malformed native PipeWire receipt record");}
template<class Cycle> void validate(const Cycle &c,bool claimed,bool capture){
    require(c.channels && c.channels<=256 && c.returnedChannels<=256 && c.requestedFrames<=65536 && c.monitorFrames<=c.requestedFrames);
    if constexpr(std::is_same_v<Cycle,PipeWireCaptureCycle>)require(c.rawFrames<=c.requestedFrames);
    for(std::size_t k=std::size_t(c.returnedChannels);k<256;++k)require(c.returns[k]==PipeWirePortLeaseResult{});
    if(!claimed)return;
    require(c.requestedFrames && c.returnClock==c.original && c.original.duration==c.requestedFrames && c.returnedChannels==c.channels);
    for(std::size_t k=0;k<c.channels;++k){const auto &p=c.returns[k];require((p.status==PipeWireBufferStatus::Ready ||
        (capture && p.status==PipeWireBufferStatus::Silence)) && p.acquired && p.queueResult && *p.queueResult>=0);}
}
struct Encoder {
    std::string out;
    void integer(std::uint64_t v,unsigned n){for(unsigned k=0;k<n;++k){out.push_back(char(v&255));v>>=8;}}
    void boolean(bool b){integer(b?1:0,1);}
    void clock(const DeviceBlockClock &c){integer(c.position,8);integer(c.duration,8);integer(c.monotonicNs,8);
        integer(c.id,4);integer(c.cycle,4);integer(c.rateNumerator,4);integer(c.rateDenominator,4);
        integer(std::bit_cast<std::uint64_t>(c.delay),8);boolean(c.xrun);boolean(c.discontinuity);}
    template<class Cycle> void common(const PipeWireReceiptRecord<Cycle> &r){
        const auto &c=r.original;require(r.generation && r.sequence!=UINT64_MAX && c.channels && c.channels<=256 && c.returnedChannels<=256);
        integer(r.generation,8);integer(r.sequence,8);clock(c.original);boolean(c.returnClock.has_value());if(c.returnClock)clock(*c.returnClock);
        integer(c.requestedFrames,4);integer(c.channels,4);integer(c.returnedChannels,8);
    }
    void ports(const std::array<PipeWirePortLeaseResult,256> &ports,std::uint64_t count){
        for(std::uint64_t k=0;k<count;++k){const auto &p=ports[std::size_t(k)];require(unsigned(p.status)<=3);
            integer(unsigned(p.status),1);boolean(p.acquired);boolean(p.queueResult.has_value());
            if(p.queueResult)integer(std::bit_cast<std::uint32_t>(*p.queueResult),4);}
    }
};
struct Decoder {
    std::string_view in;std::size_t offset=0;
    std::uint64_t integer(unsigned n){require(n<=8 && offset<=in.size() && n<=in.size()-offset);std::uint64_t v=0;
        for(unsigned k=0;k<n;++k)v|=std::uint64_t(static_cast<unsigned char>(in[offset++]))<<(k*8);
        return v;}
    bool boolean(){const auto b=integer(1);require(b<=1);return b!=0;}
    DeviceBlockClock clock(){DeviceBlockClock c;c.position=integer(8);c.duration=integer(8);c.monotonicNs=integer(8);
        c.id=std::uint32_t(integer(4));c.cycle=std::uint32_t(integer(4));c.rateNumerator=std::uint32_t(integer(4));c.rateDenominator=std::uint32_t(integer(4));
        c.delay=std::bit_cast<std::int64_t>(integer(8));c.xrun=boolean();c.discontinuity=boolean();return c;}
    template<class Cycle> PipeWireReceiptRecord<Cycle> common(){PipeWireReceiptRecord<Cycle> r;
        r.generation=integer(8);r.sequence=integer(8);auto &c=r.original;c.original=clock();if(boolean())c.returnClock=clock();
        c.requestedFrames=std::uint32_t(integer(4));c.channels=std::uint32_t(integer(4));c.returnedChannels=integer(8);
        require(r.generation && r.sequence!=UINT64_MAX && c.channels && c.channels<=256 && c.returnedChannels<=256);return r;}
    void ports(std::array<PipeWirePortLeaseResult,256> &ports,std::uint64_t count){
        for(std::uint64_t k=0;k<count;++k){auto &p=ports[std::size_t(k)];const auto status=integer(1);require(status<=3);
            p.status=PipeWireBufferStatus(status);p.acquired=boolean();if(boolean())p.queueResult=std::bit_cast<std::int32_t>(std::uint32_t(integer(4)));}
    }
};
}
std::string encodePipeWireReceipt(const PipeWireReceipt &v){
    Encoder e;e.out.reserve(4096);e.out="SCPWREC1";e.integer(1,2);e.integer(0,2);e.integer(1,1); // POSIX CLOCK_MONOTONIC nanoseconds.
    if(const auto *r=std::get_if<PipeWireReceiptRecord<PipeWireCaptureCycle>>(&v)){
        validate(r->original,r->original.allReturned,true);
        e.integer(0,1);e.common(*r);e.integer(r->original.rawFrames,4);e.integer(r->original.monitorFrames,4);e.boolean(r->original.allReturned);
        e.ports(r->original.returns,r->original.returnedChannels);
    }else{const auto &renderRecord=std::get<PipeWireReceiptRecord<PipeWireRenderCycle>>(v);validate(renderRecord.original,renderRecord.original.allQueued,false);e.integer(1,1);e.common(renderRecord);
        e.integer(renderRecord.original.monitorFrames,4);e.boolean(renderRecord.original.warmup);e.boolean(renderRecord.original.finalContent);e.boolean(renderRecord.original.allQueued);
        e.ports(renderRecord.original.returns,renderRecord.original.returnedChannels);}
    return std::move(e.out);
}
PipeWireReceipt decodePipeWireReceipt(std::string_view input){
    require(input.size()<=4096 && input.size()>=14 && input.substr(0,8)=="SCPWREC1");Decoder d{input,8};
    require(d.integer(2)==1 && d.integer(2)==0 && d.integer(1)==1);const auto kind=d.integer(1);PipeWireReceipt out;
    if(kind==0){auto r=d.common<PipeWireCaptureCycle>();r.original.rawFrames=std::uint32_t(d.integer(4));r.original.monitorFrames=std::uint32_t(d.integer(4));
        r.original.allReturned=d.boolean();d.ports(r.original.returns,r.original.returnedChannels);validate(r.original,r.original.allReturned,true);out=std::move(r);
    }else{require(kind==1);auto r=d.common<PipeWireRenderCycle>();r.original.monitorFrames=std::uint32_t(d.integer(4));
        r.original.warmup=d.boolean();r.original.finalContent=d.boolean();r.original.allQueued=d.boolean();d.ports(r.original.returns,r.original.returnedChannels);validate(r.original,r.original.allQueued,false);out=std::move(r);}
    require(d.offset==input.size());return out;
}
} // namespace soundcurrent::daw
