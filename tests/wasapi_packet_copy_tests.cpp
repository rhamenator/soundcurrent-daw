// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_packet_copy.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct Lease {
    std::vector<float> sdk = std::vector<float>(3*101);
    PreparedWasapiInput *input = nullptr;
    unsigned releases = 0, calls = 0;
    std::uint32_t releasedFrames = 0;
    std::int32_t hr = 0;
    bool order = true;
    WasapiPacket observed;
    static std::int32_t release(void *context, std::uint32_t frames) noexcept {
        auto &s = *static_cast<Lease *>(context);
        ++s.releases; s.releasedFrames = frames;
        s.order &= !s.calls;
        std::fill(s.sdk.begin(),s.sdk.end(),-999.f); // Simulate immediate SDK reuse.
        return s.hr;
    }
    static void packet(void *context, const WasapiPacket &p) noexcept {
        auto &s = *static_cast<Lease *>(context);
        ++s.calls; s.order &= s.releases == 1;
        s.order &= !p.data || p.data != reinterpret_cast<const std::byte *>(s.sdk.data());
        s.observed = p; s.observed.data = nullptr; // Retain metadata only.
        if (s.input) s.input->consume(p);
    }
    WasapiPacketCopyResult deliver(PreparedWasapiPacketCopy &copy, WasapiPacket p,
                                  bool consumer = true) {
        rt_audit::reset(); WasapiPacketCopyResult result;
        { rt_audit::Guard guard;
          result = copy.deliver(p,{this,release,consumer ? packet : nullptr}); }
        auto c = rt_audit::counts;
        require(!c.cppAllocate && !c.cppFree && !c.cAllocate && !c.cFree && !c.blockingLock,
                "Packet copy/release/processing performed prohibited RT work");
        return result;
    }
};
void releasedData() {
    auto session = makeOneTrackSession("Released packet","Selected channels");
    session.tracks.front().layout = {LayoutKind::Stereo,2};
    session.tracks.front().eq.bands.front().gainDb = -6;
    CaptureConfig config; config.layout = session.tracks.front().layout;
    config.maximumCallbackFrames = 64; config.slabFrames = 256; config.poolSlabs = 8;
    CapturePipe raw(config), wet(config);
    AudioBridge bridge(session,session.tracks.front().id,raw,{64,1,101,CaptureBackend::Wasapi,{}},&wet);
    PreparedWasapiInput input(bridge,{3,128,99,{2,0},{}});
    PreparedWasapiPacketCopy copy(3,128);
    Lease lease; lease.input = &input;
    std::vector<float> expected;
    for (unsigned n = 0; n < 101; ++n) {
        lease.sdk[3*n] = -2.f+float(n)*.01f; lease.sdk[3*n+1] = 100.f;
        lease.sdk[3*n+2] = 2.f+float(n)*.02f;
        expected.push_back(lease.sdk[3*n+2]); expected.push_back(lease.sdk[3*n]);
    }
    const WasapiPacket p{reinterpret_cast<const std::byte *>(lease.sdk.data()),lease.sdk.size()*4,
                         101,wasapiDiscontinuity,12345,1000000};
    auto r = lease.deliver(copy,p);
    require(r.error == WasapiPacketCopyError::None && r.releaseHresult == 0 && r.releaseAttempted &&
            r.callbackInvoked && lease.order && lease.releases == 1 && lease.calls == 1 &&
            lease.releasedFrames == 101 && bridge.status() == AudioBridgeStatus::Complete,
            "SDK release must precede processing exactly once");
    require(lease.observed.frames == p.frames && lease.observed.flags == p.flags &&
            lease.observed.devicePosition == p.devicePosition && lease.observed.qpc100ns == p.qpc100ns,
            "Copied packet timing/flags changed");
    std::vector<float> captured, processed; CapturedSlab slab;
    while (raw.acquire(slab)) {
        captured.insert(captured.end(),slab.interleaved.begin(),slab.interleaved.end());
        require(raw.release(slab),"Raw slab retirement failed");
    }
    while (wet.acquire(slab)) {
        processed.insert(processed.end(),slab.interleaved.begin(),slab.interleaved.end());
        require(wet.release(slab),"Processed slab retirement failed");
    }
    require(captured == expected && processed.size() == expected.size() && processed != expected &&
            *std::max_element(captured.begin(),captured.end()) > 1.f,
            "Released SDK data corrupted raw/EQ/channel order or float headroom");
}
void failurePaths() {
    PreparedWasapiPacketCopy copy(3,128);
    for (unsigned n = 0; n < 9; ++n) {
        auto session = makeOneTrackSession("Packet failures","Raw");
        CaptureConfig config; config.maximumCallbackFrames = 64;
        config.slabFrames = 256; config.poolSlabs = 8;
        CapturePipe raw(config);
        AudioBridge bridge(session,session.tracks.front().id,raw,{64,1,32,CaptureBackend::Wasapi,{}});
        PreparedWasapiInput input(bridge,{3,128,99,{0},{}});
        Lease lease;
        lease.input = &input;
        WasapiPacket p{reinterpret_cast<const std::byte *>(lease.sdk.data()),lease.sdk.size()*4,32,0,99,777};
        if (n == 0) { p.data = reinterpret_cast<const std::byte *>(1); p.bytes = 0; p.flags = wasapiSilent; }
        if (n == 1) p.frames = 129;
        if (n == 2) p.data = nullptr;
        if (n == 3) p.bytes = 3;
        if (n == 4) lease.hr = -1;
        if (n == 5) p.frames = 0;
        if (n == 6) p.flags = wasapiTimestampError | wasapiDiscontinuity;
        if (n == 8) lease.hr = 1; // Successful nonzero HRESULT must not become a failure.
        auto r = lease.deliver(copy,p,n != 7);
        require(r.releaseAttempted && lease.releases == 1 && lease.releasedFrames == p.frames &&
                r.releaseHresult == lease.hr && lease.order,"Failure lost/doubled SDK release");
        require(r.callbackInvoked == (n == 0 || n == 2 || n == 3 || n == 6 || n == 8),
                "Failed release/invalid extent reached processing or error metadata was hidden");
        if (r.callbackInvoked) require(lease.observed.flags == p.flags &&
                lease.observed.devicePosition == p.devicePosition && lease.observed.qpc100ns == p.qpc100ns,
                "Fault/silence metadata changed");
        if (n == 2 || n == 3) require(r.error == WasapiPacketCopyError::InvalidData && lease.observed.bytes == 0 &&
                input.firstError() && input.firstError()->error == WasapiInputError::InvalidData &&
                bridge.firstFault(),"Invalid SDK backing was fabricated or its first fault hidden");
        if (!r.callbackInvoked) require(bridge.status() == AudioBridgeStatus::Ready && !input.firstPacket(),
                                       "Failed release/extent advanced raw capture");
        if (n == 0 || n == 8) require(bridge.status() == AudioBridgeStatus::Complete,
                                     "Released silent/valid packet failed to complete");
        if (n == 6) require(input.firstError() &&
                input.firstError()->error == WasapiInputError::TimestampUnavailable && bridge.firstFault(),
                "SDK timestamp failure was normalized away");
    }
}
void admission() {
    ResourceLedger ledger(4096,"Native packet copy");
    const auto before = ledger.usage();
    for (auto [channels,frames] : {std::pair{0u,64u},{257u,64u},{2u,0u},{2u,65537u},{256u,65536u}}) {
        bool rejected = false;
        try { PreparedWasapiPacketCopy copy(channels,frames,ledger); }
        catch (const ProjectError &) { rejected = true; }
        require(rejected && ledger.usage().reservedBytes == before.reservedBytes &&
                ledger.usage().owners == before.owners,"Failed copy admission consumed credit");
    }
    {
        PreparedWasapiPacketCopy copy(3,128,ledger);
        require(ledger.usage().reservedBytes == copy.chargedBytes() && ledger.usage().owners == 1,
                "Prepared native copy escaped memory budget");
        Lease lease;
        auto admitted = ledger.usage();
        for (unsigned n = 0; n < 100; ++n) {
            lease.releases = lease.calls = 0;
            auto r = lease.deliver(copy,{reinterpret_cast<const std::byte *>(lease.sdk.data()),lease.sdk.size()*4,
                                        32,0,n*32,1000+n});
            require(r.callbackInvoked && ledger.usage() == admitted,"Callback changed admitted storage");
        }
    }
    require(!ledger.usage().reservedBytes && !ledger.usage().owners,"Retired copy leaked memory credit");
}
} // namespace
int main() {
    try { releasedData(); failurePaths(); admission();
          std::cout << "Packet copy: SDK reuse, release/error order, raw/EQ fidelity and bounded admission passed\n";
          return 0; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
