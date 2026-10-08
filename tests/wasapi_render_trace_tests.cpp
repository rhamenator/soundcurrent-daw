// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_render_trace.hpp>
#include "rt_audit.hpp"
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace soundcurrent::daw;
namespace {
void require(bool ok,const char *message) { if (!ok) throw std::runtime_error(message); }
void unchanged(const ResourceLedger &ledger) {
    require(!ledger.usage().reservedBytes && !ledger.usage().owners,"Render trace retained refused/retired credit");
}
}
int main() {
    try {
        static_assert(!std::is_copy_constructible_v<WasapiRenderTrace> &&
                      !std::is_move_constructible_v<WasapiRenderTrace>);
        ResourceLedger ledger(1024*1024,"Native render trace tests");
        for (unsigned n=0;n<7;++n) {
            bool refused=false;
            try {
                WasapiRenderTrace invalid(n==0?0:n==1?257:2,n==2?0:n==3?65537:4,
                    n==4?0:n==5?std::uint32_t(WasapiRenderTrace::maximumSampleValues):8,
                    n==6?WasapiRenderTrace::maximumRows+1:4,ledger);
            } catch(const ProjectError &e) { refused=e.code()==ErrorCode::InvalidState; }
            require(refused,"Malformed trace configuration admitted"); unchanged(ledger);
        }
        ledger.configure(32);
        bool refused=false;
        try { WasapiRenderTrace invalid(2,4,8,4,ledger); }
        catch(const ProjectError &e) { refused=e.code()==ErrorCode::ResourceLimit; }
        require(refused,"Trace escaped project-wide memory budget"); unchanged(ledger);
        ledger.configure(1024*1024);
        {
            WasapiRenderTrace trace(3,4,8,4,ledger);
            require(ledger.usage().owners==1 && ledger.usage().reservedBytes==trace.chargedBytes(),
                    "Trace storage was not fully charged before preparation");
            require(!trace.prepare(2,4) && !trace.prepare(3,5) && trace.prepare(3,4) && !trace.prepare(3,4),
                    "Trace admitted mismatched native shape or reuse");
            bool liveReadRefused=false;
            try { (void)trace.samples(); }
            catch(const ProjectError &e) { liveReadRefused=e.code()==ErrorCode::InvalidState; }
            require(liveReadRefused,"Control read an unjoined sample producer");
            std::array<float,6> data{1.25f,-2.f,.5f,3.f,0.f,-.75f};
            const auto original=data;
            WasapiRenderClock clock; clock.submittedFrames=491; clock.contentSubmittedFrames=11;
            clock.startupFrames=480; clock.clockPosition=91; clock.clockFrequency=48000;
            clock.qpc100ns=789; clock.paddingFrames=123;
            rt_audit::reset(); rt_audit::active=true;
            auto first=trace.beforeRelease(clock,2,WasapiRenderAction::Continue,data);
            // Simulated release destroys borrowed backing. Copy must already be owned.
            data.fill(99.f); trace.released(first,-2147467259,2);
            auto abort=trace.beforeRelease(clock,2,WasapiRenderAction::Abort,{});
            trace.released(abort,0,0);
            auto finish=trace.beforeRelease(clock,2,WasapiRenderAction::Finish,original);
            trace.released(finish,0,2);
            trace.acquireFailed(clock,2,-2147024891);
            auto lost=trace.beforeRelease(clock,2,WasapiRenderAction::Continue,original);
            trace.released(lost,0,2);
            trace.sealAfterJoin(); rt_audit::active=false;
            require(!rt_audit::counts.cppAllocate && !rt_audit::counts.cppFree &&
                    !rt_audit::counts.cAllocate && !rt_audit::counts.cFree && !rt_audit::counts.blockingLock,
                    "Trace allocated, retired or blocked in its native window");
            const auto rows=trace.observations(); const auto samples=trace.samples();
            require(rows.size()==4 && samples.size()==12 && !trace.malformed() && trace.lostRows()==1 &&
                    trace.lostSampleFrames()==2,"Bounded trace did not expose exact metadata loss");
            require(std::equal(original.begin(),original.end(),samples.begin()) &&
                    std::equal(original.begin(),original.end(),samples.begin()+6),
                    "Trace changed raw float headroom or retained borrowed backing");
            require(rows[0].releaseHresult==-2147467259 && rows[0].releaseObserved &&
                    rows[0].copiedFrames==2 && rows[0].clock.contentSubmittedFrames==11 &&
                    rows[0].clock.qpc100ns==789 && rows[0].clock.clockPosition==91 &&
                    rows[0].clock.paddingFrames==123,"Trace normalized failed release or timing domains");
            require(rows[1].action==WasapiRenderAction::Abort && rows[1].releasedFrames==0 &&
                    !rows[1].samplesComplete && !rows[1].copiedFrames && rows[2].sampleOffsetValues==6 &&
                    rows[2].samplesComplete && !rows[3].acquired && !rows[3].releaseObserved &&
                    rows[3].acquireHresult==-2147024891,"Abort/acquire failure was relabeled as submitted audio");
        }
        unchanged(ledger);
        {
            WasapiRenderTrace trace(2,4,1,4,ledger); require(trace.prepare(2,4),"Small bank preparation failed");
            std::array<float,4> data{};
            auto token=trace.beforeRelease({},2,WasapiRenderAction::Finish,data); trace.released(token,0,2);
            trace.sealAfterJoin();
            require(trace.samples().empty() && trace.lostSampleFrames()==2 && !trace.lostRows() &&
                    !trace.observations()[0].samplesComplete,"Sample exhaustion silently truncated a lease");
        }
        unchanged(ledger);
        {
            WasapiRenderTrace trace(2,4,4,4,ledger); require(trace.prepare(2,4),"Malformed probe preparation failed");
            auto token=trace.beforeRelease({},2,WasapiRenderAction::Continue,{});
            trace.released(token,0,1); trace.released(token,0,1); trace.sealAfterJoin();
            require(trace.malformed() && !trace.observations()[0].samplesComplete,
                    "Malformed backing/extent/double release became a complete trace");
        }
        unchanged(ledger);
        {
            WasapiRenderTrace trace(2,4,4,4,ledger); require(trace.prepare(2,4),"Unreleased probe preparation failed");
            std::array<float,4> data{};
            trace.beforeRelease({},2,WasapiRenderAction::Continue,data); trace.sealAfterJoin();
            require(trace.malformed() && !trace.observations()[0].releaseObserved,
                    "Unreleased lease was silently sealed as complete");
        }
        unchanged(ledger);
        std::cout << "Native render trace: owned samples, raw headroom/errors, abort, bounded loss, admission and RT audit passed\n";
        return 0;
    } catch(const std::exception &e) { rt_audit::active=false; std::cerr<<e.what()<<'\n'; return 1; }
}
