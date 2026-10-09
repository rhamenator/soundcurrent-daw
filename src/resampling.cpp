// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/resampling.hpp>
#include <samplerate.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace soundcurrent::daw {
namespace {
void require(bool value,const char *message,ErrorCode code=ErrorCode::InvalidState) {
    if(!value) throw ProjectError(code,message);
}
void room(Frame total,std::size_t add) {
    require(add<=std::uint64_t(std::numeric_limits<Frame>::max()-total),
            "Resampling frame counter overflow",ErrorCode::InvalidParameter);
}
}
std::uint32_t resamplingSourceContextFrames(const ResamplingConfig &c) {
    (void)SourceFrameMap(c.sourceRate,c.projectRate);
    if(c.sourceRate==c.projectRate) return 0;
    const auto numerator=std::uint64_t(340239)*std::max(c.sourceRate,c.projectRate);
    const auto denominator=std::uint64_t(2381)*c.projectRate;
    return std::uint32_t((numerator+denominator-1)/denominator+2);
}
std::size_t resamplingPayloadBytes(const ResamplingConfig &c) {
    (void)SourceFrameMap(c.sourceRate,c.projectRate);
    require(c.channels && c.channels<=256 && c.maximumInputFrames &&
            c.maximumInputFrames<=65536 && c.maximumOutputFrames && c.maximumOutputFrames<=65536,
            "Invalid resampling block/channel admission",ErrorCode::InvalidParameter);
    PayloadCharge charge("Prepared resampling",c.memoryBudgetBytes);
    charge.add(8192);
    // Pinned0.2.2 best-sinc: 3*lrint((340239/2381)*256+1) floats/channel
    // plus sentinel/channels. 512KiB/channel bounds that allocation and small
    // private/state structs. This is an admission allowance, not measured RSS.
    if(c.sourceRate!=c.projectRate) charge.add(c.channels,512*1024);
    charge.add(std::size_t(c.maximumInputFrames)*c.channels,sizeof(float));
    return charge.bytes();
}
struct PreparedResampler::State {
    struct Delete {void operator()(SRC_STATE *p) const noexcept {if(p) src_delete(p);}};
    ResourceLease lease;
    ResamplingConfig config;
    std::unique_ptr<SRC_STATE,Delete> converter;
    std::vector<float> input;
    std::size_t available=0,used=0;
    bool closing=false,padding=false;
    std::uint32_t paddingRemaining=0;
    Frame outputEnd=0;
    ResamplingStatistics stats;
    State(ResourceLease l,ResamplingConfig c):lease(std::move(l)),config(std::move(c)){}
};
PreparedResampler::PreparedResampler(ResamplingConfig config) {
    const auto bytes=resamplingPayloadBytes(config);
    auto lease=config.resources ? config.resources->reserve(bytes) : ResourceLease{};
    auto s=std::make_unique<State>(std::move(lease),std::move(config));
    if(s->config.sourceRate!=s->config.projectRate) {
        int error=0;s->converter.reset(src_new(SRC_SINC_BEST_QUALITY,int(s->config.channels),&error));
        require(bool(s->converter) && !error,"Cannot prepare pinned best-sinc converter",ErrorCode::Io);
        const auto ratio=double(s->config.projectRate)/s->config.sourceRate;
        require(src_is_valid_ratio(ratio) && !src_set_ratio(s->converter.get(),ratio),
                "Cannot prepare fixed source/project ratio",ErrorCode::InvalidParameter);
    }
    s->input.resize(std::size_t(s->config.maximumInputFrames)*s->config.channels);
    state_=std::move(s);
}
PreparedResampler::~PreparedResampler()=default;
void PreparedResampler::push(std::span<const float> in,bool end) {
    auto &s=*state_;const auto channels=s.config.channels;
    require(!s.stats.failed && !s.closing && s.available==s.used,"Resampler is failed, closing or still owns pending input");
    require(in.size()%channels==0 && in.size()/channels<=s.config.maximumInputFrames &&
            (!in.empty() || end),"Invalid resampling input span",ErrorCode::InvalidParameter);
    const auto frames=in.size()/channels;room(s.stats.acceptedInputFrames,frames);
    const auto outputEnd=end ? SourceFrameMap(s.config.sourceRate,s.config.projectRate)
        .projectFramesForSource(s.stats.acceptedInputFrames+Frame(frames)) : 0;
    for(const auto x:in) require(std::isfinite(x),"Non-finite resampling input",ErrorCode::InvalidParameter);
    std::copy(in.begin(),in.end(),s.input.begin());
    s.available=frames;s.used=0;s.closing=end;s.stats.acceptedInputFrames+=Frame(frames);
    if(end) {
        s.outputEnd=outputEnd;
        s.paddingRemaining=(s.config.sourceRate+s.config.projectRate-1)/s.config.projectRate+1;
    }
    s.stats.wantsInput=false;
}
std::uint32_t PreparedResampler::pull(std::span<float> out) {
    auto &s=*state_;const auto channels=s.config.channels;
    require(!s.stats.failed,"Resampler processing failure requires retirement");
    require(!out.empty() && out.size()%channels==0 && out.size()/channels<=s.config.maximumOutputFrames,
            "Invalid resampling output span",ErrorCode::InvalidParameter);
    if(s.stats.drained || s.stats.wantsInput) return 0;
    if(!s.converter) {
        const auto count=std::min(s.available-s.used,out.size()/channels);
        std::copy_n(s.input.data()+s.used*channels,count*channels,out.data());
        s.used+=count;s.stats.consumedInputFrames+=Frame(count);s.stats.producedOutputFrames+=Frame(count);
        s.stats.wantsInput=!s.closing && s.used==s.available;
        s.stats.drained=s.closing && s.used==s.available;
        return std::uint32_t(count);
    }
    if(s.closing && s.stats.producedOutputFrames==s.outputEnd) {
        if(s.stats.consumedInputFrames!=s.stats.acceptedInputFrames) s.stats.failed=true;
        require(s.stats.consumedInputFrames==s.stats.acceptedInputFrames,
                "Aligned resampling ended before consuming owned input",ErrorCode::Io);
        s.stats.drained=true;return 0;
    }
    if(s.closing && s.used==s.available && s.paddingRemaining) {
        s.padding=true;s.available=std::min(s.paddingRemaining,s.config.maximumInputFrames);s.used=0;
        s.paddingRemaining-=std::uint32_t(s.available);
        std::fill_n(s.input.data(),s.available*channels,0.f);
    }
    const auto capacity=s.closing ? std::min<Frame>(Frame(out.size()/channels),s.outputEnd-s.stats.producedOutputFrames)
                                  : Frame(out.size()/channels);
    room(s.stats.producedOutputFrames,std::size_t(capacity));
    SRC_DATA data{};data.data_in=s.input.data()+s.used*channels;data.data_out=out.data();
    data.input_frames=long(s.available-s.used);data.output_frames=long(capacity);
    data.end_of_input=s.closing && s.padding && !s.paddingRemaining;
    data.src_ratio=double(s.config.projectRate)/s.config.sourceRate;
    try {
    const auto error=src_process(s.converter.get(),&data);
    require(!error,"Pinned resampling process failed",ErrorCode::Io);
    require(data.input_frames_used>=0 && data.input_frames_used<=data.input_frames &&
            data.output_frames_gen>=0 && data.output_frames_gen<=data.output_frames,
            "Resampling library count invariant failed",ErrorCode::Io);
    for(std::size_t n=0;n<std::size_t(data.output_frames_gen)*channels;++n)
        require(std::isfinite(out[n]),"Non-finite resampling output",ErrorCode::Io);
    s.used+=std::size_t(data.input_frames_used);
    if(!s.padding) s.stats.consumedInputFrames+=data.input_frames_used;
    s.stats.producedOutputFrames+=data.output_frames_gen;
    if(!data.input_frames_used && !data.output_frames_gen)
        require(s.used==s.available,"Resampling failed to make progress",ErrorCode::Io);
    s.stats.wantsInput=!s.closing && s.used==s.available;
    if(s.closing && !data.input_frames_used && !data.output_frames_gen && !s.paddingRemaining)
        require(s.stats.producedOutputFrames==s.outputEnd,"Resampling ended before aligned duration",ErrorCode::Io);
    return std::uint32_t(data.output_frames_gen);
    } catch(...) {s.stats.failed=true;s.stats.wantsInput=false;throw;}
}
ResamplingStatistics PreparedResampler::statistics() const noexcept {return state_->stats;}
} // namespace soundcurrent::daw
