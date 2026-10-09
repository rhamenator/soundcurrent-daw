// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/mix_reader.hpp>
#include <soundcurrent/export.hpp>
#include <soundcurrent/wave_validation.hpp>
#include <soundcurrent/project_store.hpp>
#include <nlohmann/json.hpp>
#include "rt_audit.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool v,const char *message){++checks;if(!v)throw std::runtime_error(message);}
std::vector<float> live(const std::filesystem::path &root,const Session &s,Frame first,Frame end,std::uint32_t block,ResourceLedger ledger){
    auto channels=s.tracks[0].layout.channels;MixPlan plan;plan.output=s.tracks[0].layout;TrackMix route{s.tracks[0].id,{}};
    for(std::uint32_t ch=0;ch<channels;++ch)route.channels.push_back({ch,ch,1});
    plan.tracks.push_back(route);
    MixPlaybackConfig config;config.graph.startFrame=first;config.endFrame=end;config.slabFrames=512;config.graph.maximumFrames=block;config.graph.resources=ledger;
    MixPlayback mix(s,plan,config);ReadAheadOptions options;options.resources=ledger;options.cache.resources=ledger;MixReader reader(mix,root,s,options);
    std::vector<float> slab(std::size_t(block)*channels),result(std::size_t(end-first)*channels);std::array<float *,256> output{};
    for(std::uint32_t ch=0;ch<channels;++ch)output[ch]=slab.data()+std::size_t(ch)*block;
    for(Frame at=first;at<end;){while(reader.fillRound()){}auto n=std::uint32_t(std::min<Frame>(block,end-at));MixPlaybackReport report;
        {rt_audit::Guard guard;report=mix.process({output.data(),channels},n);}
        check(!report.missingTrackFrames && report.mix.status==ProcessStatus::Ok && report.timelineFrames==n,"Derived live playback lost samples");
        for(std::uint32_t f=0;f<n;++f)for(std::uint32_t ch=0;ch<channels;++ch)result[std::size_t(at-first+f)*channels+ch]=output[ch][f];
        at+=n;
    }
    return result;
}
}
int run(const std::vector<std::string> &args){try{
    check(args.size()==3,"Expected owned project root and relative derived artifact");auto root=utf8Path(args[1]);std::string relative=args[2];validateRelativeMediaPath(relative);
    auto path=root/utf8Path(relative);std::ifstream receiptFile(path.parent_path()/"complete.json");nlohmann::json receipt;receiptFile>>receipt;
    check(receipt["complete"]==true && receipt["processor"]=="soundcurrent.stretch-rubberband4-r3-v1","Expected completed stretch artifact");
    Asset asset;asset.relativePath=relative;asset.sha256=receipt.at("audioSha256");asset.sampleRate=receipt.at("rate");asset.frames=receipt.at("writtenFrames");asset.layout={LayoutKind::Stereo,2};
    auto s=makeOneTrackSession("Derived stretch","Audio",asset.sampleRate);s.assets={asset};s.tracks[0].layout=asset.layout;s.tracks[0].eq={};Clip c;c.assetId=asset.id;c.lengthFrames=asset.frames;s.tracks[0].clips={c};s.exportEndFrame=asset.frames;
    ResourceLedger ledger(128*1024*1024,"Derived workflow");rt_audit::reset();auto expected=live(root,s,0,asset.frames,37,ledger);check(!ledger.usage().reservedBytes,"Derived reader grant retained");
    check(live(root,s,0,asset.frames,511,ledger)==expected,"Callback partitions changed derived audio");
    auto split=s;applySessionEdits(split,{SplitClip{s.tracks[0].id,c.id,Id::generate(),4097}});check(live(root,split,0,asset.frames,127,ledger)==expected,"Derived split restarted stretch phase");
    auto suffix=live(root,s,257,asset.frames,113,ledger);check(std::equal(suffix.begin(),suffix.end(),expected.begin()+257*2),"Derived seek changed waveform");
    auto crop=s;applySessionEdits(crop,{CropClip{s.tracks[0].id,c.id,37,37,asset.frames-37}});check(live(root,crop,37,asset.frames,113,ledger)==std::vector<float>(expected.begin()+37*2,expected.end()),"Derived crop restarted processor");
    std::filesystem::create_directory(root/"exports");ExportSpec spec(s.tracks[0].id);spec.endFrame=asset.frames;spec.blockFrames=127;ExportOptions options;options.resources=ledger;
    auto exported=exportTrackWav(root,s,root/"exports/stretch.wav",spec,options);ApprovedMediaRoot approved(root,ledger);auto file=approved.open("exports/stretch.wav",16*1024*1024);std::vector<float> actual;
    validateApprovedWave(file,{}, {},[&](std::uint64_t,std::span<const double> samples){for(auto sample:samples)actual.push_back(float(sample));});
    check(actual==expected && exported.peak>1 && exported.frames==asset.frames,"Derived offline export differs from live/headroom");
    ProjectStore(root).save(s);check(ProjectStore(root).load()==s && hashMediaFile(path)==asset.sha256,"Derived Save/reopen or immutable bytes differ");
    check(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree && !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,"Derived callback performed audited allocation/free/lock");
    std::cout<<"derived_shared_live_export_checks="<<checks<<" callback_audit=0 seek_split_crop_equal=true save_reopen=true\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}

#ifdef _WIN32
int wmain(int argc,wchar_t **argv){std::vector<std::string> args;for(int i=0;i<argc;++i){auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 1;std::string s(std::size_t(n),'\0');if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr))return 1;s.pop_back();args.push_back(std::move(s));}return run(args);}
#else
int main(int argc,char **argv){return run({argv,argv+argc});}
#endif
