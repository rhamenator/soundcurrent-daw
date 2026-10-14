// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/comp_media_review.hpp>
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <soundcurrent/positioned_resampling.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
namespace soundcurrent::daw {
namespace {
void check(bool ok,const char *m,ErrorCode code=ErrorCode::MediaMismatch){if(!ok)throw ProjectError(code,m);}
WaveValidationLimits waveLimits(const Asset &a,std::uint64_t bytes){
    WaveValidationLimits l;l.maximumFrames=std::uint64_t(a.frames);l.maximumChannels=a.layout.channels;
    l.blockFrames=512;l.maximumBytesRead=bytes*4+65536;l.maximumIoOperations=16000000;return l;
}
// Decode through the already pinned approved object. The container was fully
// validated before this reader is constructed; random reads never reopen paths.
class Reader {
  public:
    Reader(ApprovedMediaFile &f,const Asset &a,std::uint64_t maximum,const std::function<void()> &poll,ResourceLedger ledger,std::stop_token stop)
        :file_(f),extent_(inspectApprovedWaveData(f,waveLimits(a,maximum),WaveExtentPolicy::ExactContainer,poll)),
         lease_(ledger.reserve(65536)),raw_(65536),poll_(poll),stop_(stop){}
    void read(Frame first,std::span<float> out){
        const auto &format=extent_.format;const auto channels=format.channels,width=format.bitsPerSample/8;
        check(first>=0 && out.size()%channels==0 && std::uint64_t(first)<=format.frames &&
            out.size()/channels<=format.frames-std::uint64_t(first),"Comp media read exceeds pinned extent");
        for(std::size_t at=0;at<out.size();){
            poll_();const auto samples=std::min<std::size_t>(out.size()-at,raw_.size()/width);
            file_.readAt(extent_.offset+(std::uint64_t(first)*channels+at)*width,{raw_.data(),samples*width},stop_);
            for(std::size_t n=0;n<samples;++n){std::uint64_t bits=0;
                for(unsigned k=0;k<width;++k){const auto shift=format.bigEndian?width-1-k:k;bits|=std::uint64_t(static_cast<unsigned char>(raw_[n*width+k]))<<(8*shift);}
                double value=0;
                if(format.encoding==WaveEncoding::Float32)value=double(std::bit_cast<float>(std::uint32_t(bits)));
                else if(format.encoding==WaveEncoding::Float64)value=std::bit_cast<double>(bits);
                else if(format.encoding==WaveEncoding::Unsigned8)value=(double(bits)-128)/128;
                else {const auto sign=std::uint64_t(1)<<(format.bitsPerSample-1);
                    const auto integer=bits<sign?std::int64_t(bits):std::int64_t(bits)-std::int64_t(sign*2);
                    value=double(integer)/std::ldexp(1.0,int(format.bitsPerSample)-1);}
                out[at+n]=float(value);check(std::isfinite(out[at+n]),"Nonfinite decoded comp media");
            }
            at+=samples;
        }
    }
  private:
    ApprovedMediaFile &file_;WaveDataExtent extent_;ResourceLease lease_;std::vector<char> raw_;
    const std::function<void()> &poll_;std::stop_token stop_;
};
bool same(float a,float b){return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
}
ReviewedCompMedia reviewCompMedia(const std::filesystem::path &root,const Session &current,
    const CompHandlePlanContext &context,const CompProcessingRecipe &recipe,
    const std::array<std::optional<Asset>,2> &outputs,ResourceLedger ledger,CompMediaReviewLimits limits,std::stop_token stop){
    check(limits.maximumSourceBytes>0 && limits.maximumSourceBytes<=8ULL*1024*1024*1024 &&
        limits.maximumResultBytes>0 && limits.maximumResultBytes<=8ULL*1024*1024*1024 &&
        limits.deadlineMilliseconds>=100 && limits.deadlineMilliseconds<=3600000,"Invalid comp media review bounds",ErrorCode::InvalidParameter);
    verifyCompProcessingCurrent(current,context,recipe.plan());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(limits.deadlineMilliseconds);
    const std::function<void()> poll=[&]{check(!stop.stop_requested(),"Comp media review canceled",ErrorCode::Canceled);
        check(std::chrono::steady_clock::now()<deadline,"Comp media review deadline exceeded",ErrorCode::ResourceLimit);};
    poll();auto retained=ledger.reserve(65536);ApprovedMediaRoot approved(root,ledger,{2});CompMediaReviewCounts counts;
    const auto validateAsset=[&](ApprovedMediaFile &file,const Asset &a,std::uint64_t maximum,bool result){
        const auto wave=validateApprovedWave(file,waveLimits(a,maximum),stop,{},poll);
        check(wave.frames==std::uint64_t(a.frames) && wave.rate==a.sampleRate && wave.channels==a.layout.channels &&
            std::string(wave.sourceSha256.begin(),wave.sourceSha256.end())==a.sha256 &&
            (!result || wave.encoding==WaveEncoding::Float32),"Comp media format/hash differs from declaration");file.verifyUnchanged();
    };
    const auto source=[&](const Asset &a){poll();auto f=approved.open(a.relativePath,limits.maximumSourceBytes,stop);validateAsset(f,a,limits.maximumSourceBytes,false);
        for(const auto &evidence:a.nativeEvidence){poll();auto file=approved.open(evidence.relativePath,std::min(limits.maximumSourceBytes,evidence.bytes),stop);
            check(file.size()==evidence.bytes,"Comp source evidence extent changed");const auto hash=file.digest(stop,poll);
            check(std::string(hash.begin(),hash.end())==evidence.sha256,"Comp source evidence changed");file.verifyUnchanged();}
    };
    const auto &plan=recipe.plan();const auto &transition=plan.handles().transition();
    const std::array<const CompHandleEnvelope *,2> envelopes{&plan.handles().outgoing(),&plan.handles().incoming()};
    const std::array<const CompFragmentProcessing *,2> processing{&plan.outgoing(),&plan.incoming()};
    for(unsigned side=0;side<2;++side){poll();const auto &e=envelopes[side]->data();const auto &p=*processing[side];
        source(e.raw);source(e.base);source(e.retainedAsset);if(e.nominalAsset)source(*e.nominalAsset);
        if(p.domain==CompProcessingDomain::ExistingMedia){check(!outputs[side],"Existing media received an unexpected result");continue;}
        check(outputs[side].has_value(),"Comp media review requires both generated results");const auto &a=*outputs[side];
        const auto rate=p.domain==CompProcessingDomain::PhysicalArtifact?e.base.sampleRate:e.projectRate;
        const auto frames=p.pcm.prefixFrames+p.pcm.currentFrames+p.pcm.suffixFrames;
        check(a.sampleRate==rate && a.frames==frames && a.layout==e.base.layout && a.nativeEvidence.empty() &&
            a.relativePath=="media/comp-results/"+recipe.operation().str()+"/"+a.id.str()+".wav","Comp result is outside its recipe geometry/path");
        check(std::none_of(current.assets.begin(),current.assets.end(),[&](const auto &existing){return existing.id==a.id;}),"Comp result reused a current asset identity");
        if(outputs[1-side])check(a.id!=outputs[1-side]->id,"Comp results reused an asset identity");
        auto oldFile=approved.open(e.base.relativePath,limits.maximumSourceBytes,stop);
        auto newFile=approved.open(a.relativePath,limits.maximumResultBytes,stop);
        validateAsset(oldFile,e.base,limits.maximumSourceBytes,false);validateAsset(newFile,a,limits.maximumResultBytes,true);
        Reader old(oldFile,e.base,limits.maximumSourceBytes,poll,ledger,stop),result(newFile,a,limits.maximumResultBytes,poll,ledger,stop);
        const auto channels=e.base.layout.channels;const auto oldMap=clipSourceMap(e.expected,e.base.sampleRate,e.projectRate);
        PreparedPositionedResampling oldKernel(oldMap,channels);
        const SourceFrameMap expanded(e.base.sampleRate,e.projectRate,e.clipOutputOrigin,e.expected.playbackRate);
        const auto aligned=expanded.advanced(e.expected.startFrame-e.projectFirst);PreparedPositionedResampling newKernel(aligned,channels);
        const auto window=std::max(oldKernel.maximumSourceWindowFrames(512),newKernel.maximumSourceWindowFrames(512));
        PayloadCharge scratch("Comp media comparison slabs",SIZE_MAX);scratch.add(window,channels*sizeof(float));scratch.add(3*512,channels*sizeof(float));
        auto credit=ledger.reserve(scratch.bytes());std::vector<float> input(window*channels),baseline(512*channels),actual(512*channels),physical(512*channels);
        if(p.domain==CompProcessingDomain::PhysicalArtifact){
            for(Frame at=0;at<e.base.frames;at+=512){poll();const auto n=std::size_t(std::min<Frame>(512,e.base.frames-at));
                old.read(at,{baseline.data(),n*channels});result.read(at+p.pcm.prefixFrames,{physical.data(),n*channels});
                for(std::size_t f=0;f<n;++f){const auto frame=at+Frame(f);
                    const bool permitted=(p.blends.prefix && frame<p.blends.prefix->frames && frame>=p.blends.prefix->permittedFirst && frame<p.blends.prefix->permittedEnd) ||
                        (p.blends.suffix && frame>=e.base.frames-p.blends.suffix->frames && frame>=p.blends.suffix->permittedFirst && frame<p.blends.suffix->permittedEnd);
                    if(!permitted)for(unsigned ch=0;ch<channels;++ch){check(same(baseline[f*channels+ch],physical[f*channels+ch]),"Comp result changed current PCM outside permitted blend");++counts.physicalSamplesCompared;}
                }
            }
        }
        const auto render=[&](Reader &reader,const PreparedPositionedResampling &kernel,const SourceFrameMap &map,Frame sourceFrames,Frame at,std::uint32_t n,std::vector<float> &out){
            const auto r=kernel.sourceRange(map,at,n,sourceFrames);reader.read(r.first,{input.data(),r.frames*channels});
            kernel.process(map,at,sourceFrames,r.first,{input.data(),r.frames*channels},{out.data(),std::size_t(n)*channels});};
        for(Frame at=0;at<e.expected.lengthFrames;at+=512){poll();const auto n=std::uint32_t(std::min<Frame>(512,e.expected.lengthFrames-at));
            render(old,oldKernel,oldMap,e.base.frames,at,n,baseline);
            if(p.domain==CompProcessingDomain::PhysicalArtifact)render(result,newKernel,aligned,a.frames,at,n,actual);
            else result.read(at+p.pcm.prefixFrames,{actual.data(),std::size_t(n)*channels});
            for(unsigned f=0;f<n;++f){const auto project=e.expected.startFrame+at+f;
                if(project<transition.startFrame || project>=transition.endFrame)for(unsigned ch=0;ch<channels;++ch){
                    check(same(baseline[std::size_t(f)*channels+ch],actual[std::size_t(f)*channels+ch]),"Comp result changed project PCM outside requested transition");++counts.projectSamplesCompared;}
            }
        }
        oldFile.verifyUnchanged();newFile.verifyUnchanged();
    }
    // Sources establishing retained/raw geometry may differ from current media.
    for(const auto *envelope:envelopes){const auto &e=envelope->data();source(e.raw);source(e.base);source(e.retainedAsset);if(e.nominalAsset)source(*e.nominalAsset);}
    for(const auto &output:outputs)if(output){poll();auto file=approved.open(output->relativePath,limits.maximumResultBytes,stop);
        const auto digest=file.digest(stop,poll);check(std::string(digest.begin(),digest.end())==output->sha256,"Reviewed comp result changed before pair completion");file.verifyUnchanged();}
    poll();verifyCompProcessingCurrent(current,context,recipe.plan());
    return {std::move(retained),recipe.digest(),context,outputs,counts};
}
} // namespace soundcurrent::daw
