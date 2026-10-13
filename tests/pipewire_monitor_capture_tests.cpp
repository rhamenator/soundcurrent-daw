// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/pipewire_monitor_capture.hpp>
#include "rt_audit.hpp"
#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> auto rt(F f) { rt_audit::Guard guard; return f(); }
template<class F> void refuses(F f) {
    try { f(); } catch (const ProjectError &) { ++checks; return; }
    throw std::runtime_error("Invalid PipeWire capture admission accepted");
}
constexpr ClockStreamIdentity identity{71, 2, 9};
constexpr std::uint64_t position = (std::uint64_t(1) << 54) + 73;
constexpr std::uint64_t nanos = (std::uint64_t(1) << 63) + 987;
CaptureFanoutConfig fanConfig(unsigned channels = 3, unsigned slots = 4) {
    CaptureFanoutConfig c; c.capture.identity = identity; c.capture.channels = channels;
    c.capture.maximumPacketFrames = 17; c.capture.slots = slots; c.monitorSlots = slots; return c;
}
MonitorClockHandoffConfig clockConfig(unsigned slots = 4) {
    MonitorClockHandoffConfig c; c.captureIdentity = identity; c.slots = slots;
    c.timestampDomain = MonitorClockDomain::PosixMonotonic100ns; return c;
}
DeviceBlockClock clock(unsigned offset = 0, unsigned frames = 17) {
    return {position + offset, frames, nanos + std::uint64_t(offset) * 1000000000 / 48000,
            19, 4 + offset / 17, 1, 48000, -37, false, false};
}
struct Planar {
    std::array<std::array<float,17>,3> samples{};
    std::array<const float *,3> views{};
    Planar() {
        for (unsigned c = 0; c < 3; ++c) {
            views[c] = samples[c].data();
            for (unsigned f = 0; f < 17; ++f) samples[c][f] = float(f + c * 17) / 4 - 6;
        }
        samples[0][0] = std::bit_cast<float>(0x80000000U);
        samples[1][0] = std::bit_cast<float>(0x7fc01234U);
        samples[2][0] = std::bit_cast<float>(0x7f800000U);
    }
};
void exactPackets() {
    PreparedCaptureFanout f(fanConfig()); PreparedMonitorClockHandoff h(clockConfig());
    ResourceLedger resources(65536,"PipeWire capture test");const auto before = resources.usage();
    {
        PreparedPipeWireMonitorCapture c(f,h,{65536,resources});check(resources.usage().reservedBytes > before.reservedBytes,"Scratch was not charged");
        Planar p;check(!c.endAfterJoin(),"Invented an unjoined native end");
        for (unsigned packet = 0; packet < 20; ++packet) {
            const auto actual = clock(packet * 17);
            check(rt([&]{return c.process(actual,p.views,17);}) == PipeWireCaptureCode::Accepted,"Matching native packet refused");
            for (unsigned side = 0; side < 2; ++side) {
                CaptureBankView v;check((side ? f.acquireMonitor(v) : f.acquireRaw(v)) == CaptureBankRead::Ready,"Missing raw/monitor packet");
                check(v.token.original.position.value == actual.position && v.token.original.qpc.value == actual.monotonicNs / 100 && v.token.original.flags == 0,"Native coordinate conversion changed");
                for (unsigned frame = 0; frame < 17; ++frame) for (unsigned channel = 0; channel < 3; ++channel)
                    check(std::bit_cast<std::uint32_t>(v.interleaved[frame * 3 + channel]) == std::bit_cast<std::uint32_t>(p.samples[channel][frame]),"Float bits/channel order changed");
                check(side ? f.releaseMonitor(v) : f.releaseRaw(v),"Packet release failed");
            }
            MonitorClockRecord<CaptureClockReading> reading;
            check(h.read(reading).code == MonitorClockCode::Ready && reading.original.identity == identity && reading.original.position.value == actual.position && reading.original.qpc.value == actual.monotonicNs / 100,"Clock handoff differs from raw coordinate");
        }
        rt([&]{c.requestStop();});const auto later = clock(20 * 17);
        check(rt([&]{return c.process(later,p.views,17);}) == PipeWireCaptureCode::Stopped,"Stop accepted more raw packets");
        c.finishAfterJoin();c.finishAfterJoin();const auto end = c.endAfterJoin();
        check(end && end->callbacksJoined && !end->fault && end->acceptedPackets == 20 && end->acceptedRawFrames == 340 && end->acceptedMonitorFrames == 340,"Joined end counts incorrect");
        check(end->firstAccepted == clock() && end->lastAccepted == clock(19 * 17),"Original nanoseconds/cycle/delay lost");
        check(f.producerDone() && h.captureDone(),"Capture EOF did not follow join");
    }
    check(resources.usage().reservedBytes == before.reservedBytes,"Scratch credit did not retire");
}
void badClock(PipeWireCaptureCause cause, const std::function<void(DeviceBlockClock &)> &mutate) {
    PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(clockConfig());PreparedPipeWireMonitorCapture c(f,h);
    Planar p;check(rt([&]{return c.process(clock(),p.views,17);}) == PipeWireCaptureCode::Accepted,"First fault fixture packet refused");
    auto bad = clock(17);mutate(bad);
    check(rt([&]{return c.process(bad,p.views,17);}) == PipeWireCaptureCode::Fault,"Invalid native clock accepted");
    const auto fault = c.firstFault();check(fault && fault->cause == cause && fault->original == bad,"Original first fault lost");
    check(rt([&]{return c.process(clock(34),p.views,17);}) == PipeWireCaptureCode::Fault && c.firstFault()->original == bad,"Fault allowed resumption/overwriting first evidence");
    c.finishAfterJoin();check(c.endAfterJoin()->acceptedPackets == 1 && f.rawAcceptedFrames() == 17,"Refused packet entered raw bank");
}
void refusals() {
    badClock(PipeWireCaptureCause::InvalidQuantum,[](auto &c){c.duration=18;});
    badClock(PipeWireCaptureCause::InvalidQuantum,[](auto &c){c.duration=0;});
    badClock(PipeWireCaptureCause::RateChanged,[](auto &c){c.rateDenominator=44100;});
    badClock(PipeWireCaptureCause::RateChanged,[](auto &c){c.rateNumerator=2;});
    badClock(PipeWireCaptureCause::MissingTimestamp,[](auto &c){c.monotonicNs=0;});
    badClock(PipeWireCaptureCause::ClockChanged,[](auto &c){++c.id;});
    badClock(PipeWireCaptureCause::CycleDiscontinuity,[](auto &c){++c.cycle;});
    badClock(PipeWireCaptureCause::Discontinuity,[](auto &c){c.xrun=true;});
    badClock(PipeWireCaptureCause::Discontinuity,[](auto &c){c.discontinuity=true;});
    badClock(PipeWireCaptureCause::PositionJump,[](auto &c){++c.position;});
    badClock(PipeWireCaptureCause::TimestampReset,[](auto &c){c.monotonicNs=nanos;});
    badClock(PipeWireCaptureCause::PositionOverflow,[](auto &c){c.position=std::numeric_limits<std::uint64_t>::max()-3;});
    for (unsigned mode = 0; mode < 3; ++mode) {
        PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(clockConfig());PreparedPipeWireMonitorCapture c(f,h);Planar p;
        if (mode == 0) p.views[1]=nullptr;
        const auto count = mode == 1 ? 2U : 3U;
        check(rt([&]{return c.process(clock(),{p.views.data(),count},mode == 2 ? 16 : 17);}) == PipeWireCaptureCode::Fault,"Invalid backing accepted");
        check(!f.rawAcceptedFrames(),"Invalid backing published raw audio");c.finishAfterJoin();
    }
    PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(clockConfig());
    refuses([&]{PreparedPipeWireMonitorCapture c(f,h,{16,{}});});
    auto mismatch = clockConfig();mismatch.captureIdentity.stream=99;PreparedMonitorClockHandoff other(mismatch);
    refuses([&]{PreparedPipeWireMonitorCapture c(f,other);});
    auto windows = clockConfig();windows.timestampDomain=MonitorClockDomain::WindowsQpc100ns;
    PreparedMonitorClockHandoff foreignClock(windows);
    refuses([&]{PreparedPipeWireMonitorCapture c(f,foreignClock);});
    f.finish();refuses([&]{PreparedPipeWireMonitorCapture c(f,h);});
}
void domainsAndWrap() {
    PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(clockConfig());
    PreparedPipeWireMonitorCapture c(f,h);Planar p;
    auto first=clock();first.cycle=UINT32_MAX;
    auto second=clock(17);second.cycle=0;
    check(rt([&]{return c.process(first,p.views,17);})==PipeWireCaptureCode::Accepted &&
          rt([&]{return c.process(second,p.views,17);})==PipeWireCaptureCode::Accepted,"Native cycle wrap rejected");
    c.finishAfterJoin();check(c.endAfterJoin()->lastAccepted->cycle==0,"Wrapped cycle receipt changed");
    PreparedMonitorClockHandoff posix(clockConfig());
    check(rt([&]{return posix.publish(CaptureClockReading{identity,{1},{10000}});})==MonitorClockCode::Fault,
          "Windows QPC capture admitted to POSIX lane");
    PreparedMonitorClockHandoff posixRender(clockConfig());
    check(rt([&]{return posixRender.publish(RenderClockReading{posixRender.config().renderIdentity,{1},{10000},48000});})==MonitorClockCode::Fault,
          "Windows QPC render admitted to POSIX lane");
    auto windows=clockConfig();windows.timestampDomain=MonitorClockDomain::WindowsQpc100ns;
    PreparedMonitorClockHandoff qpc(windows);
    check(rt([&]{return qpc.publishMonotonic(CaptureClockReading{identity,{1},{10000}});})==MonitorClockCode::Fault,
          "POSIX capture admitted to Windows QPC lane");
    PreparedMonitorClockHandoff qpcRender(windows);
    check(rt([&]{return qpcRender.publishMonotonic(RenderClockReading{qpcRender.config().renderIdentity,{1},{10000},48000});})==MonitorClockCode::Fault,
          "POSIX render admitted to Windows QPC lane");
    PreparedMonitorClockHandoff both(clockConfig());
    check(rt([&]{return both.publishMonotonic(CaptureClockReading{identity,{1},{10000}});})==MonitorClockCode::Accepted &&
          rt([&]{return both.publishMonotonic(RenderClockReading{both.config().renderIdentity,{1},{10000},48000});})==MonitorClockCode::Accepted,
          "Declared matching POSIX clock pair refused");
    MonitorClockRecord<CaptureClockReading> input;MonitorClockRecord<RenderClockReading> output;
    check(both.read(input).code==MonitorClockCode::Ready && both.read(output).code==MonitorClockCode::Ready &&
          input.original.qpc.value==10000 && output.original.qpc.value==10000,"POSIX clock pair coordinate changed");
    auto invalid=clockConfig();invalid.timestampDomain=MonitorClockDomain(999);
    refuses([&]{PreparedMonitorClockHandoff rejected(invalid);});
}
void formats() {
    for(unsigned channels : {1U,2U,3U,16U,256U})
        for(unsigned rate : {8000U,44100U,48000U,96000U,192000U,384000U})
            for(unsigned frames : {1U,17U}) {
                auto config=fanConfig(channels,2);config.capture.sampleRate=rate;
                PreparedCaptureFanout f(config);PreparedMonitorClockHandoff h(clockConfig());
                PreparedPipeWireMonitorCapture c(f,h);
                std::vector<float> data(std::size_t(channels)*17);
                std::vector<const float *> planes(channels);
                for(unsigned channel=0;channel<channels;++channel) {
                    planes[channel]=data.data()+std::size_t(channel)*17;
                    for(unsigned frame=0;frame<17;++frame)
                        data[std::size_t(channel)*17+frame]=float(channel)*3+float(frame)/8-4;
                }
                auto actual=clock(0,frames);actual.rateDenominator=rate;
                check(rt([&]{return c.process(actual,planes,frames);})==PipeWireCaptureCode::Accepted,"Declared rate/layout/short quantum refused");
                for(unsigned side=0;side<2;++side) {
                    CaptureBankView view;check((side?f.acquireMonitor(view):f.acquireRaw(view))==CaptureBankRead::Ready,"Format packet missing");
                    for(unsigned frame=0;frame<frames;++frame)for(unsigned channel=0;channel<channels;++channel)
                        check(std::bit_cast<std::uint32_t>(view.interleaved[std::size_t(frame)*channels+channel])==
                              std::bit_cast<std::uint32_t>(planes[channel][frame]),"Wide/short planar packet changed");
                    check(side?f.releaseMonitor(view):f.releaseRaw(view),"Format packet release failed");
                }
                c.finishAfterJoin();
            }
    auto config=fanConfig(1,2);config.capture.maximumPacketFrames=65536;
    PreparedCaptureFanout f(config);PreparedMonitorClockHandoff h(clockConfig());PreparedPipeWireMonitorCapture c(f,h);
    std::vector<float> samples(65536,2.5F);const std::array<const float *,1> planes{samples.data()};
    check(rt([&]{return c.process(clock(0,65536),planes,65536);})==PipeWireCaptureCode::Accepted && f.rawAcceptedFrames()==65536,
          "Maximum admitted quantum failed");c.finishAfterJoin();
}
void pressures() {
    for (unsigned rawPressure = 0; rawPressure < 2; ++rawPressure) {
        PreparedCaptureFanout f(fanConfig(3,2));PreparedMonitorClockHandoff h(clockConfig(2));PreparedPipeWireMonitorCapture c(f,h);Planar p;
        for (unsigned n=0;n<2;++n)check(rt([&]{return c.process(clock(n*17),p.views,17);})==PipeWireCaptureCode::Accepted,"Pressure setup failed");
        if (!rawPressure) {
            for (unsigned n=0;n<2;++n)for(unsigned side=0;side<2;++side){CaptureBankView v;check((side?f.acquireMonitor(v):f.acquireRaw(v))==CaptureBankRead::Ready,"Pressure release missing");check(side?f.releaseMonitor(v):f.releaseRaw(v),"Pressure release failed");}
        }
        check(rt([&]{return c.process(clock(34),p.views,17);})==PipeWireCaptureCode::Fault,"Full bounded queue accepted more data");
        check(c.firstFault()->cause==(rawPressure?PipeWireCaptureCause::RawOrMonitorRejected:PipeWireCaptureCause::ClockRejected),"Pressure fault source changed");
        check(f.rawAcceptedFrames()==(rawPressure?34U:51U),"Clock failure erased the preceding raw publication");c.finishAfterJoin();
    }
    PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(clockConfig());PreparedPipeWireMonitorCapture c(f,h);
    rt([&]{c.unavailable(AudioBridgeStatus::DeviceLost);c.unavailable(AudioBridgeStatus::RateChanged);});
    check(!c.firstFault() && c.firstUnavailable()==AudioBridgeStatus::DeviceLost,"Control notification overwrote original error");
    c.finishAfterJoin();check(c.endAfterJoin()->fault && c.endAfterJoin()->fault->cause==PipeWireCaptureCause::NativeUnavailable && !c.endAfterJoin()->fault->original,"Unavailable without callback invented a native clock");
}
void minimumQuantum(){
    PreparedCaptureFanout f(fanConfig());PreparedMonitorClockHandoff h(clockConfig());Planar p;
    PipeWireMonitorCaptureOptions o;o.minimumQuantumFrames=17;PreparedPipeWireMonitorCapture c(f,h,o);
    const auto small=clock(0,16);check(rt([&]{return c.process(small,p.views,17);})==PipeWireCaptureCode::Fault &&
        c.firstFault()->cause==PipeWireCaptureCause::InvalidQuantum && c.firstFault()->original==small && !f.rawAcceptedFrames(),"Storage minimum violated without original fault/no-publication");
    c.finishAfterJoin();
    PreparedCaptureFanout f2(fanConfig());PreparedMonitorClockHandoff h2(clockConfig());o.minimumQuantumFrames=18;
    refuses([&]{PreparedPipeWireMonitorCapture bad(f2,h2,o);});
}
}
int main() {
    try {
        rt_audit::reset();minimumQuantum();exactPackets();refusals();pressures();domainsAndWrap();formats();
        const auto a=rt_audit::counts;check(!a.cppAllocate && !a.cppFree,"RT C++ allocation/free");
#ifdef SC_WRAP_LIBC
        check(!a.cAllocate && !a.cFree && !a.blockingLock,"RT C allocation/free/lock");
#endif
        std::cout<<checks<<" PipeWire planar capture checks passed; raw float bits, original nanoseconds, bounded pressure and refusal; RT allocate/free/lock=0; actual daemon=false; complete native monitor adapter=false\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}
