// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/clip_processing.hpp>
#include <soundcurrent/project_store.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool value,const char *message) {
    ++checks;
    if(!value) throw std::runtime_error(message);
}
template<class F> void refuses(F f) {
    try {f();} catch(const ProjectError &) {++checks;return;}
    throw std::runtime_error("Invalid crossfade edit accepted");
}
struct Fixture {
    Session session=makeOneTrackSession("Crossfade", "Audio");
    Id track=session.tracks[0].id;
    Clip outgoing,incoming;
    Fixture() {
        Asset asset;asset.relativePath="media/owned.wav";
        asset.sha256=std::string(64,'a');asset.frames=4096;
        outgoing.assetId=incoming.assetId=asset.id;
        outgoing.startFrame=100;outgoing.sourceFrame=53;outgoing.lengthFrames=900;
        incoming.startFrame=400;incoming.sourceFrame=23;incoming.lengthFrames=600;
        outgoing.processing.fadeIn={0,7,ClipFadeCurve::Smoothstep,2};
        incoming.processing.fadeOut={500,600,ClipFadeCurve::Linear,1};
        session.assets.push_back(asset);
        session.tracks[0].clips={outgoing,incoming};
        validate(session);
    }
    SetClipCrossfade edit(ClipFadeCurve curve=ClipFadeCurve::Linear) const {
        return {track,outgoing.id,incoming.id,500,757,curve};
    }
};
void stateAndSamples() {
    for(const auto curve:{ClipFadeCurve::Linear,ClipFadeCurve::EqualPower,ClipFadeCurve::Smoothstep}) {
        Fixture f;const auto original=f.session;EditHistory history(f.session);
        check(history.structural({f.edit(curve)}),"Paired fade edit missing");
        const auto selected=f.session;
        const auto &a=selected.tracks[0].clips[0], &b=selected.tracks[0].clips[1];
        check(a.processing.fadeIn==f.outgoing.processing.fadeIn && b.processing.fadeOut==f.incoming.processing.fadeOut,
              "Opposite fades changed");
        auto restoredA=a,restoredB=b;
        restoredA.processing=f.outgoing.processing;restoredB.processing=f.incoming.processing;
        check(restoredA==f.outgoing && restoredB==f.incoming && selected.assets==original.assets,
              "Crossfade altered media, geometry or source timing");
        check(a.processing.fadeOut.startFrame+a.startFrame==500 && b.processing.fadeIn.startFrame+b.startFrame==500 &&
              a.processing.fadeOut.endFrame+a.startFrame==757 && b.processing.fadeIn.endFrame+b.startFrame==757,
              "Paired fades do not share project anchors");
        PreparedClipProcessing out(a.processing),in(b.processing);
        for(Frame frame=500;frame<757;++frame) {
            double left,right;
            {rt_audit::Guard guard;left=out.gainAt(frame-a.startFrame);right=in.gainAt(frame-b.startFrame);}
            const auto phase=double(frame-500)/256.;
            const auto expectedRight=curve==ClipFadeCurve::Linear ? phase : curve==ClipFadeCurve::Smoothstep ? phase*phase*(3-2*phase) : std::sin(phase*std::numbers::pi/2);
            const auto expectedLeft=curve==ClipFadeCurve::EqualPower ? std::cos(phase*std::numbers::pi/2) : 1-expectedRight;
            check(std::abs(left-expectedLeft)<2e-15 && std::abs(right-expectedRight)<2e-15,"Independent project-frame gain oracle differs");
            const auto invariant=curve==ClipFadeCurve::EqualPower ? left*left+right*right : left+right;
            check(std::abs(invariant-1)<4e-15,"Paired gain/power law differs");
            const auto mixed=1.8*(left+right);
            check(mixed>=1.8-1e-14 && mixed<=1.8*std::sqrt(2.)+1e-14,"Float headroom or predictable correlated peak differs");
        }
        check(out.gainAt(499-a.startFrame)==1 && in.gainAt(499-b.startFrame)==0 &&
              out.gainAt(757-a.startFrame)==0 && in.gainAt(757-b.startFrame)==1,"Transition endpoints differ");
        check(decodeProject(encodeProject(selected))==selected,"Crossfade project round trip differs");
        const auto resources=history.resources();
        check(!history.structural({f.edit(curve)}) && history.resources()==resources,"Repeated fade edit adds history");
        check(history.undo() && f.session==original && history.redo() && f.session==selected,"Paired Undo/Redo is not atomic");
    }
}
void groupedPair() {
    Fixture f;
    std::vector<SessionEdit> edits;
    for(unsigned n=0;n<8;++n) {
        auto track=makeAudioTrack("Linked",{},f.session.sampleRate);
        auto out=f.outgoing,in=f.incoming;out.id=Id::generate();in.id=Id::generate();
        out.sourceFrame+=Frame(n)*3;in.sourceFrame+=Frame(n)*5;
        in.sourceTiming={1,3};track.clips={out,in};
        edits.push_back(SetClipCrossfade{track.id,out.id,in.id,500,757,ClipFadeCurve::Smoothstep});
        f.session.tracks.push_back(track);
    }
    validate(f.session);const auto original=f.session;EditHistory history(f.session);
    check(history.structural(edits),"Eight-track paired edit missing");
    const auto selected=f.session;
    check(history.undo() && f.session==original && history.redo() && f.session==selected,
          "Eight-track paired edit is not one Undo transaction");
    check(history.undo(),"Group restore missing");
    auto bad=std::get<SetClipCrossfade>(edits.back());bad.endFrame=1001;edits.back()=bad;
    const auto resources=history.resources();refuses([&]{history.structural(edits);});
    check(f.session==original && history.resources()==resources,"Last-track refusal partly changed group");
}
void refusalsAndPreservation() {
    Fixture f;f.session.tracks[0].clips[0].processing.gainDb=4;
    f.session.tracks[0].clips[1].processing.polarityInverted=true;
    EditHistory history(f.session);
    const auto original=f.session;
    auto missing=f.edit();missing.incoming=Id::generate();
    auto same=f.edit();same.incoming=same.outgoing;
    auto gap=f.edit();gap.startFrame=999;gap.endFrame=1001;
    auto before=f.edit();before.startFrame=399;
    auto empty=f.edit();empty.endFrame=empty.startFrame;
    auto reversed=f.edit();reversed.startFrame=758;
    auto negative=f.edit();negative.startFrame=-1;
    auto huge=f.edit();huge.endFrame=INT64_MAX;
    auto unknown=f.edit();unknown.curve=static_cast<ClipFadeCurve>(99);
    for(const auto &edit:{missing,same,gap,before,empty,reversed,negative,huge,unknown}) {
        const auto resources=history.resources();
        refuses([&]{history.structural({RenameTrack{f.track,"Must roll back"},edit});});
        check(f.session==original && history.resources()==resources,"Failed paired edit changed state/history");
    }
    check(history.structural({f.edit()}),"Preservation edit missing");
    check(f.session.tracks[0].clips[0].processing.gainDb==4 && f.session.tracks[0].clips[1].processing.polarityInverted,
          "Paired edit reset current gain/polarity");
    check(history.undo() && f.session==original,"Preservation Undo differs");
    // Valid clips near the frame limit: no overflow in project/local conversion.
    auto &a=f.session.tracks[0].clips[0];auto &b=f.session.tracks[0].clips[1];
    a.startFrame=INT64_MAX-1000;b.startFrame=INT64_MAX-600;
    validate(f.session);
    applySessionEdits(f.session,{SetClipCrossfade{f.track,a.id,b.id,INT64_MAX-500,INT64_MAX-100,ClipFadeCurve::Linear}});
    check(f.session.tracks[0].clips[0].processing.fadeOut.startFrame==500 &&
          f.session.tracks[0].clips[1].processing.fadeIn.startFrame==100,"Frame-limit conversion differs");
}
}
int main() {
    try {
        rt_audit::reset();stateAndSamples();groupedPair();refusalsAndPreservation();
        const auto c=rt_audit::counts;
        check(c.cppAllocate+c.cppFree+c.cAllocate+c.cFree+c.blockingLock==0,"Prepared crossfade evaluation used allocation/free/lock");
        std::cout<<"paired_crossfade_checks="<<checks<<" guarded_allocate_free_locks=0\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
