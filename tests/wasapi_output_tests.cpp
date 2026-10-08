// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/wasapi_output.hpp>
#include <soundcurrent/recording.hpp>
#include "rt_audit.hpp"
#include <algorithm>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
void require(bool ok, const char *text) { if (!ok) throw std::runtime_error(text); }
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        utf8Path("sc-native-output-Δ-" + Id::generate().str());
    Directory() { std::filesystem::create_directory(root); }
    ~Directory() { std::error_code ec; std::filesystem::remove_all(root, ec); }
};
void contracts() {
    Directory directory;
    auto session = makeOneTrackSession("Output", "Two channels");
    session.tracks.front().layout = {LayoutKind::Stereo, 2};
    CaptureConfig capture; capture.layout = session.tracks.front().layout;
    capture.maximumCallbackFrames = 64; capture.slabFrames = 256;
    CapturePipe pipe(capture);
    RecordingSpec spec; spec.projectId = session.id; spec.trackId = session.tracks.front().id;
    spec.capture = pipe.config();
    CaptureWriter writer(directory.root, spec);
    std::array<float, 64> a{}, b{};
    std::array<const float *, 2> inputs{a.data(), b.data()};
    for (Frame f = 0; f < 141;) {
        const auto count = static_cast<unsigned>(std::min<Frame>(64, 141 - f));
        for (unsigned n = 0; n < count; ++n) {
            a[n] = 2.f + float(f + n) / 1000;
            b[n] = -3.f - float(f + n) / 1000;
        }
        require(pipe.push(inputs, count, f).acceptedFrames == count, "Source capture failed");
        while (writer.drainOne(pipe)) {}
        f += count;
    }
    pipe.finish(); while (writer.drainOne(pipe)) {}
    attachRecording(session, writer.finalize(pipe));
    ProjectStore(directory.root).save(session);
    const auto sourceHash = hashMediaFile(directory.root / utf8Path(session.assets.front().relativePath));
    const auto plan = identityMix(session, std::span(&session.tracks.front().id, 1), session.tracks.front().layout);
    MixPlaybackConfig config; config.endFrame = 141; config.graph.maximumFrames = 64; config.slabFrames = 256;
    MixPlaybackRun run(directory.root, session, plan, config);
    for (unsigned n = 0; n < 5; ++n) {
        WasapiOutputConfig c{4, {3, 1}, {}};
        if (n == 0) c.channels = {1, 1};
        if (n == 1) c.channels = {0, 4};
        if (n == 2) c.nativeChannels = 0;
        if (n == 3) c.channels = {1};
        if (n == 4) c.resources = ResourceLedger(1);
        bool refused = false;
        try { PreparedWasapiOutput output(run, c); } catch (const ProjectError &) { refused = true; }
        require(refused, "Invalid map/resource admission accepted");
    }
    PreparedWasapiOutput output(run, {4, {3, 1}, {}});
    std::array<float, 257> samples{}; samples.fill(999);
    // Invalid extent, zero and over-maximum leave the graph position untouched.
    for (const auto count : {0u, 65u, 64u}) {
        const auto backing = count == 64 ? 255 : 256;
        MixPlaybackReport report;
        { rt_audit::Guard guard; report = output.process({samples.data(), std::size_t(backing)}, count); }
        require(report.status == PlaybackStatus::InvalidBuffer && run.position() == 0 &&
                samples[256] == 999, "Invalid lease changed graph or exceeded backing extent");
    }
    PreparedEq reference(session, session.tracks.front().id, 64, 1);
    std::array<float, 64> wetA{}, wetB{};
    std::array<float *, 2> wet{wetA.data(), wetB.data()};
    auto changed = session; changed.tracks.front().eq.bands.front().gainDb = -6;
    const auto &track = changed.tracks.front();
    ParameterAddress address{track.id, track.eq.id, track.eq.bands.front().id, BandParameter::GainDb};
    auto event = run.graph().parameterEvent(changed, address, 0);
    for (Frame f = 0; f < 141;) {
        if (f == 64) require(run.graph().submitImmediate(event, 17) == SubmitStatus::Accepted,
                             "Live event submission failed");
        rt_audit::reset();
        MixPlaybackReport report;
        { rt_audit::Guard guard; report = output.process({samples.data(), 256}, 64); }
        const auto counts = rt_audit::counts;
        require(!counts.cppAllocate && !counts.cppFree && !counts.cAllocate && !counts.cFree &&
                !counts.blockingLock, "Prepared output performed prohibited RT work");
        const auto expected = static_cast<unsigned>(std::min<Frame>(64, 141 - f));
        require(report.timelineFrames == expected && !report.missingTrackFrames && samples[256] == 999,
                "Output range/missing frames/backing guard differs");
        for (unsigned n = 0; n < expected; ++n) {
            a[n] = 2.f + float(f + n) / 1000; b[n] = -3.f - float(f + n) / 1000;
        }
        auto referenceEvent = reference.parameterEvent(changed, address, f);
        require(reference.process(inputs, wet, expected, f,
                                  f == 64 ? std::span(&referenceEvent, 1) : std::span<const EqEvent>{})
                    .status == ProcessStatus::Ok, "Reference processing failed");
        for (unsigned n = 0; n < 64; ++n) {
            require(samples[4*n] == 0 && samples[4*n+2] == 0 &&
                    samples[4*n+3] == (n < expected ? wetA[n] : 0.f) &&
                    samples[4*n+1] == (n < expected ? wetB[n] : 0.f),
                    "Mapping/headroom/live EQ or end slack differs");
        }
        if (f == 0) require(samples[3] > 1 && samples[1] < -1, "Float headroom clipped");
        require((report.status == PlaybackStatus::Complete) == (f + expected == 141), "Completion range differs");
        f += expected;
    }
    ImmediateAcknowledgement ack;
    require(run.graph().acknowledgement(0, ack) && ack.revision == 17 && ack.frame == 64,
            "Live applied-frame receipt differs");
    run.waitReader();
    require(hashMediaFile(directory.root / utf8Path(session.assets.front().relativePath)) == sourceHash &&
            ProjectStore(directory.root).load() == session, "Playback modified saved project/raw media");
}
}
int main() {
    try { contracts(); std::cout << "Prepared output routing/headroom/live EQ/end slack/refusal/RT/persistence pass\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
