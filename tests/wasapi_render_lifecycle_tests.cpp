// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_render_lifecycle.hpp>
#include "rt_audit.hpp"
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F> auto rt(F f){rt_audit::Guard guard;return f();}
void audit(rt_audit::Counts a){check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,"Prepared render lifecycle allocated/freed/blocked");}
constexpr std::uint64_t ticks=(std::uint64_t(1)<<63)+75000,qpc=(std::uint64_t(1)<<54)+45000;
WasapiPreparedRenderFormat format(){return {48000,2,256,64,3,96000,{100000,300000,48,48},{11,31,7}};}
struct Context {
    bool stop=false;unsigned ends=0;WasapiRenderEnd end;
    static bool poll(void *p) noexcept { return static_cast<Context *>(p)->stop; }
    static void finished(void *p,const WasapiRenderEnd &r) noexcept {
        auto &s=*static_cast<Context *>(p);++s.ends;s.end=r;
    }
};
WasapiRenderLeaseResult lease(unsigned frames,WasapiRenderAction action,std::int32_t hr=0,bool called=true){
    return {{48,ticks,96000,qpc,48,0,48},frames,action==WasapiRenderAction::Abort?0u:frames,action,hr,called};
}
void healthy(){Context c;PreparedWasapiRenderLifecycle l({&c,Context::finished,Context::poll});
    check(!l.afterJoin() && !rt([&]{return l.shouldStop();}),"Unreported render end fabricated");
    rt([&]{l.prepared(format());l.silenceReleased({WasapiSilenceKind::Startup,48,1});
        l.contentReleased(lease(64,WasapiRenderAction::Continue,2));l.started();l.padding(64);
        l.contentReleased(lease(7,WasapiRenderAction::Finish,3));});
    check(!l.afterJoin() && !c.ends && !rt([&]{return l.shouldStop();}),"Content Finish represented as completed native drain or Stop");
    rt([&]{l.silenceReleased({WasapiSilenceKind::EndGuard,17,4});l.padding(38);
        l.silenceReleased({WasapiSilenceKind::EndGuard,31,5});l.padding(0);l.queueDrained();l.stopped(6);l.finish({});});
    const auto end=l.afterJoin();check(end && c.ends==1 && end->prepared==format() && c.end.prepared==format(),"Prepared SDK descriptor lost");
    check(end->started && end->finishRequested && end->queueDrainObserved && end->stopHresult==6 && !end->invalidReceipt && !end->failure && !end->ownerException,"Healthy Stop/drain receipt altered");
    check(end->callbackLeases==2 && end->callbackFrames==71 && end->contentCommittedFrames==71 && end->startupCommittedFrames==48 && end->guardCommittedFrames==48,"Queue/content/boundary domains combined");
    check(end->lastContentLease->original.clockPosition==ticks && end->lastContentLease->original.qpc100ns==qpc && end->lastContentLease->original.clockFrequency==96000 && end->lastContentLease->releaseHresult==3 && end->lastSilenceLease->releaseHresult==5,"Original wide clock/HRESULT narrowed or normalized");
    rt([&]{l.contentReleased(lease(64,WasapiRenderAction::Continue,-17));l.stopped(-19);l.finish(WasapiStreamFailure{-20,3},true);l.padding(99);l.queueDrained();l.prepared({});});
    check(c.ends==1 && l.afterJoin()->contentCommittedFrames==71 && l.afterJoin()->stopHresult==6 && l.afterJoin()->lastPadding==0 && rt([&]{return l.shouldStop();}),"Ended SDK owner mutated original receipt");
}
void failures(){
    for(unsigned kind=0;kind<8;++kind){Context c;PreparedWasapiRenderLifecycle l({&c,Context::finished,Context::poll});
        rt([&]{l.prepared(format());});
        if(kind==0)c.stop=true; // Stop before Start: no SDK Stop was called.
        if(kind>=2)rt([&]{l.started();});
        if(kind==2)rt([&]{l.contentReleased(lease(64,WasapiRenderAction::Finish,-17));});
        if(kind==3)rt([&]{l.contentReleased(lease(64,WasapiRenderAction::Abort,0));});
        if(kind==4)rt([&]{l.contentReleased(lease(64,WasapiRenderAction::Abort,0,false));});
        if(kind==5)rt([&]{l.silenceReleased({WasapiSilenceKind::Startup,48,-21});});
        if(kind==6)rt([&]{l.contentReleased(lease(7,WasapiRenderAction::Finish));l.silenceReleased({WasapiSilenceKind::EndGuard,48,0});l.padding(0);l.queueDrained();});
        if(kind==7)rt([&]{l.contentReleased(lease(7,WasapiRenderAction::Finish));l.silenceReleased({WasapiSilenceKind::EndGuard,17,-22});});
        if(kind>=2)rt([&]{l.stopped(kind==6?-19:0);});
        rt([&]{l.finish(kind==1?std::optional(WasapiStreamFailure{-23,1}):kind==2?std::optional(WasapiStreamFailure{-17,3}):kind==6?std::optional(WasapiStreamFailure{-19,4}):std::nullopt,kind==7);});
        const auto e=l.afterJoin();check(e && c.ends==1 && e->started==(kind>=2) && bool(e->stopHresult)==(kind>=2),"Uncalled Start/Stop fabricated");
        if(kind==0)check(c.stop && e->callbackLeases==0 && !e->stopHresult,"Before-start Stop supplied SDK operation");
        if(kind==1)check(e->failure->hresult==-23 && e->failure->operation==1 && !e->stopHresult,"Failed Start normalized");
        if(kind==2)check(e->callbackFrames==64 && !e->contentCommittedFrames && e->lastContentLease->releaseHresult==-17 && e->finishRequested && !e->queueDrainObserved,"Failed release counted as committed/drained");
        if(kind==3)check(e->callbackFrames==64 && !e->contentCommittedFrames && e->lastContentLease->releaseFrames==0 && !e->finishRequested,"Abort counted as native committed content");
        if(kind==4)check(!e->callbackLeases && !e->callbackFrames && !e->contentCommittedFrames && !e->lastContentLease->callbackInvoked,"Null SDK lease fabricated callback");
        if(kind==5)check(!e->startupCommittedFrames && e->lastSilenceLease->releaseHresult==-21,"Failed startup silence counted as committed");
        if(kind==6)check(e->queueDrainObserved && e->stopHresult==-19 && e->failure->hresult==-19 && !e->invalidReceipt,"Failed Stop erased actual empty-queue observation");
        if(kind==7)check(e->ownerException && !e->guardCommittedFrames && e->lastSilenceLease->releaseHresult==-22 && !e->queueDrainObserved,"Owner exception/failed guard lost");
    }
}
void malformed(){for(unsigned kind=0;kind<10;++kind){PreparedWasapiRenderLifecycle l;auto f=format();
    if(kind==0)f.maximumFillFrames=257;
    rt([&]{l.prepared(f);});
    if(kind==1)rt([&]{l.prepared(f);});
    if(kind==2)rt([&]{l.started();});
    if(kind==3)rt([&]{l.stopped(0);});
    if(kind==4)rt([&]{l.padding(257);});
    if(kind==5)rt([&]{l.queueDrained();});
    if(kind>=6 && kind<=8){auto r=lease(64,WasapiRenderAction::Continue);if(kind==6)r.releaseFrames=65;if(kind==7)r.original.clockFrequency=48000;if(kind==8)r.action=WasapiRenderAction(99);rt([&]{l.contentReleased(r);});}
    if(kind==9)rt([&]{l.silenceReleased({WasapiSilenceKind::Startup,49,0});});
    rt([&]{l.finish({});});check(l.afterJoin()->invalidReceipt,"Inconsistent original receipt represented as valid");
}}
void warmup() {
    const WasapiWarmupLimits limits{192,500000};
    auto original=lease(64,WasapiRenderAction::Continue).original;
    Context c;PreparedWasapiRenderLifecycle l({&c,Context::finished,Context::poll},limits);
    rt([&]{l.prepared(format());l.silenceReleased({WasapiSilenceKind::Startup,48,3});});
    for(unsigned n=0;n<3;++n) {
        original.submittedFrames=48+n*64;original.clockPosition=ticks+n*128;
        original.qpc100ns=qpc+n*100000;original.contentSubmittedFrames=0;
        check(rt([&]{return l.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,original,{qpc+n*100000+1});}),"Finite warmup refused");
        rt([&]{l.warmupReleased({original,{qpc+n*100000+1},64,1,2});});
        if(!n)rt([&]{l.started();});
    }
    original.submittedFrames=240;original.clockPosition=ticks+384;original.qpc100ns=qpc+300000;
    check(rt([&]{return l.beforeLease(WasapiPreLeaseAction::Content,64,original,{qpc+300001});}),"Full warmup prevented content switch");
    auto first=lease(64,WasapiRenderAction::Finish,4);first.original=original;
    rt([&]{l.contentReleased(first);l.silenceReleased({WasapiSilenceKind::EndGuard,48,5});
        l.padding(0);l.queueDrained();l.stopped(6);l.finish({});});
    const auto end=l.afterJoin();
    check(end && c.ends==1 && !end->invalidReceipt && !end->counterOverflow && !end->preLeaseStop &&
        end->warmupLimits==limits && end->preLeaseCalls==4 && end->warmupLeaseAttempts==3 &&
        end->warmupCommittedFrames==192 && end->startupCommittedFrames==48 && end->guardCommittedFrames==48 &&
        end->callbackFrames==64 && end->contentCommittedFrames==64 && end->callbackLeases==1 && end->contentBegan,
        "Warmup advanced PCM domain or altered actual start/content/guard counts");
    check(end->firstWarmupLease->original.clockPosition==ticks && end->lastWarmupLease->original.clockPosition==ticks+256 &&
        end->lastWarmupLease->original.qpc100ns==qpc+200000 && end->lastWarmupLease->asOf.value==qpc+200001 &&
        end->lastWarmupLease->acquireHresult==1 && end->lastWarmupLease->releaseHresult==2 &&
        end->firstPreLeaseQpc100ns==qpc+1 && end->lastPreLeaseQpc100ns==qpc+300001,
        "Original SDK clock/current QPC/success HRESULT was synthesized or narrowed");
    check(!rt([&]{return l.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,original,{qpc+400000});}) &&
        l.afterJoin()->preLeaseCalls==4,"Ended owner changed its immutable receipt");
    for(unsigned kind=0;kind<9;++kind) {
        PreparedWasapiRenderLifecycle test({},limits);rt([&]{test.prepared(format());});auto r=original;
        if(kind==0) {
            check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc});}),"Acquire-failure admission failed");
            rt([&]{test.warmupReleased({r,{qpc},64,-17,{}});});
        } else if(kind==1) {
            check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc});}),"Release-failure admission failed");
            rt([&]{test.warmupReleased({r,{qpc},64,1,-19});});
        } else if(kind==2) {
            check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::Content,64,r,{qpc});}),"Content admission failed");
            check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc+1});}),"Live underflow fell back to warmup");
        } else if(kind==3) {
            check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::Abort,64,r,{qpc});}),"Pre-lease Abort acquired PCM");
        } else if(kind==4) {
            for(unsigned n=0;n<3;++n) {
                check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc+n});}),"Frame budget refused early");
                rt([&]{test.warmupReleased({r,{qpc+n},64,0,0});});
            }
            check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,1,r,{qpc+3});}),"Frame budget exceeded");
        } else if(kind==5) {
            check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc});}),"QPC budget refused early");
            rt([&]{test.warmupReleased({r,{qpc},64,0,0});});
            check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::Content,64,r,{qpc+limits.maximumWait100ns});}),"Expired readiness admitted content");
        } else if(kind==6) {
            r.clockFrequency=0;check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc});}),"Foreign clock admitted warmup");
        } else if(kind==7) {
            check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc});}),"Original QPC admission failed");
            rt([&]{test.warmupReleased({r,{qpc},64,0,0});});
            check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc-1});}),"Backward actual QPC admitted warmup");
        } else {
            check(rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc});}),"Pending receipt admission failed");
            check(!rt([&]{return test.beforeLease(WasapiPreLeaseAction::NativeWarmup,64,r,{qpc+1});}),"Second outstanding native lease admitted");
        }
        rt([&]{test.finish(kind==0?std::optional(WasapiStreamFailure{-17,2}):kind==1?std::optional(WasapiStreamFailure{-19,3}):std::nullopt);});
        const auto e=test.afterJoin();check(e && !e->callbackLeases && !e->callbackFrames && !e->contentCommittedFrames && !e->started && !e->stopHresult,"Warmup failure fabricated content/Start/Stop");
        if(kind<=1)check(!e->warmupCommittedFrames && e->warmupLeaseAttempts==1 &&
            e->lastWarmupLease->acquireHresult==(kind==0?-17:1) &&
            (kind==0?!e->lastWarmupLease->releaseHresult:e->lastWarmupLease->releaseHresult==-19),"Failed SDK warmup normalized or committed");
        if(kind==2 || kind==6 || kind==7 || kind==8)check(e->invalidReceipt && e->preLeaseStop==WasapiPreLeaseStop::Invalid,"Malformed original warmup omitted fault");
        if(kind==3)check(e->preLeaseStop==WasapiPreLeaseStop::Abort,"Abort cause lost");
        if(kind==4)check(e->preLeaseStop==WasapiPreLeaseStop::FrameBudget && e->warmupCommittedFrames==192,"Frame exhaustion erased original commits");
        if(kind==5)check(e->preLeaseStop==WasapiPreLeaseStop::Timeout,"Original QPC timeout cause lost");
    }
}
void nativeOwner(){Context c;PreparedWasapiRenderLifecycle l({&c,Context::finished,Context::poll});rt_audit::Counts sourceAudit{};
    std::thread source([&]{rt_audit::reset();rt([&]{l.prepared(format());l.started();});
        for(unsigned n=0;n<20000;++n)rt([&]{auto r=lease(64,WasapiRenderAction::Continue);r.original.clockPosition=ticks+n*128;r.original.qpc100ns=qpc+n*13333;l.contentReleased(r);});
        rt([&]{l.contentReleased(lease(7,WasapiRenderAction::Finish));l.silenceReleased({WasapiSilenceKind::EndGuard,48,0});l.padding(0);l.queueDrained();l.stopped(0);l.finish({});});sourceAudit=rt_audit::counts;
    });source.join();const auto e=l.afterJoin();audit(sourceAudit);check(e && c.ends==1 && e->callbackLeases==20001 && e->callbackFrames==1280007 && e->contentCommittedFrames==1280007 && e->queueDrainObserved && !e->invalidReceipt,"Joined long native-owner bookkeeping failed");
}
}
int main(){try{rt_audit::reset();healthy();failures();malformed();warmup();nativeOwner();audit(rt_audit::counts);std::cout<<checks<<" render SDK lifecycle checks passed; guarded allocate/free/lock=0; fake SDK only\n";return 0;}catch(const std::exception &e){rt_audit::active=false;std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
