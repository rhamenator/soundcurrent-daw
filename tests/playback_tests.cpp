// SPDX-License-Identifier: GPL-3.0-only
#include <sndfile.h>
#include "rt_audit.hpp"
#include "../src/media_io.hpp"
#include <soundcurrent/playback_reader.hpp>
#include <soundcurrent/recording.hpp>
#include <soundcurrent/rt_object_exchange.hpp>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif
using namespace soundcurrent::daw;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char *s) {
    ++checks;
    if (!ok)
        throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const ProjectError &) {
        caught = true;
    }
    check(caught, "Invalid playback admission accepted");
}
void nap() {
#ifdef _WIN32
    Sleep(1);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
}
float signal(Frame f, std::uint32_t c) {
    return float((double(f % 101) - 50) * .04 + double(c) * .125);
}
void fill(PlaybackPipe &p) {
    PlaybackSlab s;
    Frame f = p.config().startFrame;
    while (p.acquire(s)) {
        const auto n = static_cast<std::uint32_t>(
            std::min<Frame>(p.config().slabFrames, p.config().endFrame - f));
        for (std::uint32_t i = 0; i < n; ++i)
            for (std::uint32_t c = 0; c < p.config().layout.channels; ++c)
                s.interleaved[std::size_t(i) * p.config().layout.channels + c] = signal(f + i, c);
        check(p.commit(s, n, f), "Slab commit failed");
        f += n;
        if (f == p.config().endFrame) {
            p.finishReader();
            break;
        }
    }
}
void transport() {
    for (auto channels : {1u, 2u, 8u, 32u, 256u}) {
        PlaybackConfig c;
        c.layout = {channels == 1   ? LayoutKind::Mono
                    : channels == 2 ? LayoutKind::Stereo
                                    : LayoutKind::Discrete,
                    channels};
        c.slabFrames = 1024;
        c.startFrame = 10000000000LL;
        c.endFrame = c.startFrame + 10003;
        for (auto q : {16u, 64u, 127u, 512u, 2048u}) {
            PlaybackPipe p(c);
            fill(p);
            std::vector<float> values(std::size_t(channels) * q, 123);
            std::vector<float *> out(channels);
            for (std::uint32_t ch = 0; ch < channels; ++ch)
                out[ch] = values.data() + std::size_t(ch) * q;
            Frame frame = c.startFrame;
            while (frame < c.endFrame) {
                PlaybackReport r;
                {
                    rt_audit::Guard g;
                    r = p.render(out, q, frame);
                }
                check(r.missingFrames == 0 && r.startFrame == frame,
                      "Prepared playback lost samples");
                for (std::uint32_t ch = 0; ch < channels; ++ch)
                    for (std::uint32_t i = 0; i < q; ++i)
                        check(out[ch][i] == (i < r.timelineFrames ? signal(frame + i, ch) : 0.f),
                              "Playback transpose/range differs");
                frame += r.timelineFrames;
                check(r.status == (frame == c.endFrame ? PlaybackStatus::Complete
                                                       : PlaybackStatus::Running),
                      "Completion status differs");
            }
            check(p.position() == c.endFrame && p.bufferedFrames() == 0,
                  "Playback extent/occupancy differs");
        }
    }
    PlaybackConfig c;
    c.slabFrames = 256;
    c.endFrame = 1024;
    PlaybackPipe p(c);
    std::array<float, 128> output{};
    std::array<float *, 1> out{output.data()};
    PlaybackReport r;
    {
        rt_audit::Guard g;
        r = p.render(out, 128, 0);
    }
    check(r.status == PlaybackStatus::Underflow && r.missingFrames == 128 && p.position() == 128,
          "Underflow stalled timeline");
    fill(p);
    {
        rt_audit::Guard g;
        r = p.render(out, 128, 128);
    }
    check(r.staleFrames == 128 && !r.missingFrames && output[0] == signal(128, 0),
          "Late data replayed instead of skipped");
    {
        rt_audit::Guard g;
        r = p.render(out, 128, 300);
    }
    check(r.status == PlaybackStatus::TimingError && output[0] == 0,
          "Unexpected seek reset a live queue");
    PlaybackPipe tokens(c);
    PlaybackSlab slab;
    check(tokens.acquire(slab), "Cannot acquire slab");
    auto fake = slab;
    ++fake.index;
    check(!tokens.commit(fake, 256, 0) && !tokens.commit(slab, 257, 0) &&
              !tokens.commit(slab, 256, 1),
          "Invalid publication accepted");
    check(tokens.commit(slab, 256, 0), "Rejected token lost writer ownership");
    tokens.finishReader();
    check(tokens.readerFailed(), "Premature EOF not surfaced");
    rejects([&] {
        auto bad = c;
        bad.generation = 0;
        PlaybackPipe nope(bad);
    });
    rejects([&] {
        auto bad = c;
        bad.memoryBudgetBytes = 100;
        PlaybackPipe nope(bad);
    });
}
struct Temp {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / utf8Path("sc-play-Δοκιμή-" + Id::generate().str());
    Temp() {
        std::filesystem::create_directory(root);
    }
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(root, e);
    }
};
Session makeMedia(const std::filesystem::path &root) {
    auto s = makeOneTrackSession("Playback fixture", "Stereo raw");
    s.tracks.front().layout = {LayoutKind::Stereo, 2};
    RecordingSpec spec;
    spec.projectId = s.id;
    spec.trackId = s.tracks.front().id;
    spec.capture.layout = s.tracks.front().layout;
    spec.capture.slabFrames = 256;
    CapturePipe pipe(spec.capture);
    spec.capture = pipe.config();
    CaptureWriter writer(root, spec);
    std::array<float, 256> left{}, right{};
    std::array<const float *, 2> in{left.data(), right.data()};
    const Frame total = 200000;
    for (Frame f = 0; f < total;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(256, total - f));
        for (std::uint32_t i = 0; i < n; ++i) {
            left[i] = signal(f + i, 0);
            right[i] = signal(f + i, 1);
        }
        check(pipe.push(in, n, f).acceptedFrames == n, "Source fixture capture failed");
        while (writer.drainOne(pipe)) {
        }
        f += n;
    }
    pipe.finish();
    while (writer.drainOne(pipe)) {
    }
    const auto result = writer.finalize(pipe);
    attachRecording(s, result);
    auto &t = s.tracks.front();
    t.clips.clear();
    Clip a;
    a.assetId = result.asset.id;
    a.startFrame = 10150;
    a.sourceFrame = 117;
    a.lengthFrames = 50000;
    Clip b;
    b.assetId = result.asset.id;
    b.startFrame = 12500;
    b.sourceFrame = 300;
    b.lengthFrames = 1000;
    t.clips = {a, b};
    t.eq.bands.front().gainDb = 6;
    s.exportStartFrame = 9750;
    s.exportEndFrame = 62500;
    ProjectStore(root).save(s);
    return s;
}
float expected(const Session &s, Frame at, std::uint32_t ch) {
    double sum = 0;
    for (const auto &c : s.tracks.front().clips)
        if (at >= c.startFrame && at < c.startFrame + c.lengthFrames)
            sum += signal(c.sourceFrame + at - c.startFrame, ch);
    return static_cast<float>(sum);
}
PlaybackConfig configFor(const Session &s, Frame first, Frame end, std::uint64_t gen) {
    PlaybackConfig c;
    c.layout = s.tracks.front().layout;
    c.slabFrames = 256;
    c.startFrame = first;
    c.endFrame = end;
    c.generation = gen;
    return c;
}
void readerAndSeek() {
    Temp temp;
    auto s = makeMedia(temp.root);
    const auto saved = s;
    const auto first = s.exportStartFrame, end = s.exportEndFrame;
    auto config = configFor(s, first, end, 1);
    PlaybackRun run(temp.root, s, s.tracks.front().id, config);
    PreparedEq offline(s, s.tracks.front().id, 2048, 1);
    auto edited = s;
    edited.tracks.front().eq.bands.front().gainDb = -3;
    ParameterAddress address{edited.tracks.front().id, edited.tracks.front().eq.id,
                             edited.tracks.front().eq.bands.front().id, BandParameter::GainDb};
    const auto event = run.prepared().parameterEvent(edited, address, first + 997);
    check(run.submit(event) == SubmitStatus::Accepted, "Playback control event rejected");
    edited.tracks.front().eq.bands.front().gainDb = 9;
    auto immediate = run.prepared().parameterEvent(edited, address, 0);
    const auto immediateAt = first + 127 * 20;
    std::array<float, 2048> left{}, right{}, inLeft{}, inRight{}, expectedLeft{}, expectedRight{};
    std::array<float *, 2> out{left.data(), right.data()},
        reference{expectedLeft.data(), expectedRight.data()};
    std::array<const float *, 2> input{inLeft.data(), inRight.data()};
    double difference = 0;
    for (Frame at = first; at < end;) {
        const auto n = static_cast<std::uint32_t>(std::min<Frame>(127, end - at));
        if (at == immediateAt) {
            check(run.submitImmediate(immediate, 1) == SubmitStatus::Accepted,
                  "Playback immediate edit rejected");
            immediate.frame = at; // Reproduce the observed block boundary offline.
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            inLeft[i] = expected(s, at + i, 0);
            inRight[i] = expected(s, at + i, 1);
        }
        const auto events = at == immediateAt ? std::span(&immediate, 1)
                            : event.frame >= at && event.frame < at + n
                                ? std::span(&event, 1)
                                : std::span<const EqEvent>{};
        check(offline.process(input, reference, n, at, events).status == ProcessStatus::Ok,
              "Reference processing failed");
        PlaybackReport r;
        {
            rt_audit::Guard g;
            r = run.process(out, n);
        }
        check(r.timelineFrames == n && r.missingFrames == 0 && r.startFrame == at,
              "Reader lost expected timeline");
        if (at == immediateAt) {
            ImmediateAcknowledgement ack;
            check(run.acknowledgement(ack) && ack.frame == at && ack.revision == 1 &&
                      ack.eventsApplied == 1 && !run.droppedAcknowledgements(),
                  "Playback immediate receipt differs");
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            difference = std::max(difference, std::abs(double(left[i]) - expectedLeft[i]));
            difference = std::max(difference, std::abs(double(right[i]) - expectedRight[i]));
        }
        at += n;
        nap();
    }
    run.waitReader();
    check(difference == 0 && run.position() == end, "Live reader/offline samples differ");
    check(ProjectStore(temp.root).load() == saved, "Playback modified project/media");
    auto initial = std::make_unique<PlaybackRun>(temp.root, s, s.tracks.front().id,
                                                 configFor(s, first, end, 2));
    RtObjectExchange<PlaybackRun> exchange(std::move(initial));
    {
        rt_audit::Guard g;
        check(exchange.active().process(out, 127).timelineFrames == 127,
              "Initial generation failed");
    }
    auto seek = std::make_unique<PlaybackRun>(temp.root, s, s.tracks.front().id,
                                              configFor(s, 12567, end, 3));
    const auto staleEvent = exchange.active().prepared().enableEvent(false, 12600);
    check(seek->submit(staleEvent) == SubmitStatus::Invalid,
          "Old generation parameter accepted after seek");
    check(seek->submitImmediate(staleEvent, 1) == SubmitStatus::Invalid,
          "Old generation immediate edit accepted after seek");
    check(exchange.publish(seek), "Seek generation publication failed");
    {
        rt_audit::Guard g;
        check(exchange.beginReplacement(), "Seek replacement failed");
        const auto r = exchange.active().process(out, 127);
        check(r.startFrame == 12567 && r.timelineFrames == 127 && !r.missingFrames,
              "Seek played old queued samples");
        check(exchange.finishReplacement(), "Seek retirement failed");
    }
    check(exchange.collectRetired() == 1, "Old reader was not joined/retired on control");
    exchange.active().requestStop();
    {
        rt_audit::Guard g;
        check(exchange.active().process(out, 127).status == PlaybackStatus::Stopped && left[0] == 0,
              "Stop not applied by audio owner");
    }
    exchange.active().cancelReader();
    exchange.active().waitReader();
    auto failed = configFor(s, first, end, 4);
    PlaybackRun broken(temp.root, s, s.tracks.front().id, failed,
                       {64, [&](Frame at) {
                            if (at >= first + 8192)
                                throw ProjectError(ErrorCode::Io, "Injected disk read failure");
                        }});
    {
        rt_audit::Guard g;
        (void)broken.process(out, 512);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!broken.readerDone() && std::chrono::steady_clock::now() < deadline)
        nap();
    check(broken.readerDone(), "Failed reader did not finish");
    {
        rt_audit::Guard g;
        check(broken.process(out, 127).status == PlaybackStatus::ReaderFailed && left[0] == 0,
              "Read failure not surfaced to callback");
    }
    rejects([&] { broken.waitReader(); });
    rejects([&] {
        PlaybackRun bad(
            temp.root, s, s.tracks.front().id, failed,
            {64, [](Frame) { throw ProjectError(ErrorCode::Io, "Preparation canceled"); }});
    });
    auto high = s;
    high.tracks.front().clips.resize(1);
    high.tracks.front().clips.front().startFrame = std::numeric_limits<Frame>::max() - 500;
    high.tracks.front().clips.front().sourceFrame = 150000;
    high.tracks.front().clips.front().lengthFrames = 500;
    high.tracks.front().eq.bands.front().gainDb = 0;
    const auto highStart = std::numeric_limits<Frame>::max() - 475;
    PlaybackRun highRun(temp.root, high, high.tracks.front().id,
                        configFor(high, highStart, highStart + 128, 5));
    {
        rt_audit::Guard g;
        check(highRun.process(out, 128).status == PlaybackStatus::Complete,
              "Large timeline extent failed");
    }
    for (std::uint32_t i = 0; i < 128; ++i)
        check(left[i] == signal(150025 + i, 0), "Large timeline source offset overflowed");
    highRun.waitReader();
    auto altered = s;
    altered.assets.front().sha256.assign(64, '0');
    rejects([&] { PlaybackRun bad(temp.root, altered, altered.tracks.front().id, failed); });
    altered = s;
    altered.assets.front().sampleRate = 44100;
    rejects([&] { PlaybackRun bad(temp.root, altered, altered.tracks.front().id, failed); });
    altered = s;
    altered.assets.front().frames -= 1;
    rejects([&] { PlaybackRun bad(temp.root, altered, altered.tracks.front().id, failed); });
#ifndef _WIN32
    const auto path = temp.root / utf8Path(s.assets.front().relativePath);
    const auto real = path.parent_path() / "actual.wav";
    std::filesystem::rename(path, real);
    std::filesystem::create_symlink(real.filename(), path);
    rejects([&] { PlaybackRun bad(temp.root, s, s.tracks.front().id, failed); });
#endif
}
void nonfiniteMedia() {
    Temp temp;
    std::filesystem::create_directory(temp.root / "media");
    const auto path = temp.root / "media/numeric.wav";
    {
        media_io::File descriptor(path, true);
        SF_INFO info{};
        info.channels = 1;
        info.samplerate = 48000;
        info.format = SF_FORMAT_RF64 | SF_FORMAT_FLOAT;
        auto *f = sf_open_fd(descriptor.descriptor(), SFM_WRITE, &info, SF_FALSE);
        check(f != nullptr, "Numeric media creation failed");
        const std::array<float, 3> data{std::numeric_limits<float>::quiet_NaN(),
                                        std::numeric_limits<float>::infinity(), 2.f};
        check(sf_writef_float(f, data.data(), 3) == 3 && sf_close(f) == 0,
              "Numeric media write failed");
    }
    auto s = makeOneTrackSession("Numeric media", "Raw");
    Asset a;
    a.relativePath = "media/numeric.wav";
    a.frames = 3;
    a.sha256 = hashMediaFile(path);
    s.assets.push_back(a);
    Clip clip;
    clip.assetId = a.id;
    clip.lengthFrames = 3;
    s.tracks.front().clips.push_back(clip);
    PlaybackConfig c;
    c.endFrame = 3;
    PlaybackRun run(temp.root, s, s.tracks.front().id, c);
    std::array<float, 16> data{};
    std::array<float *, 1> out{data.data()};
    {
        rt_audit::Guard g;
        check(run.process(out, 16).status == PlaybackStatus::Complete, "Numeric playback failed");
    }
    check(data[0] == 0 && data[1] == 0 && data[2] == 2.f && run.sanitizedSamples() == 2,
          "Nonfinite samples not counted or float headroom clamped");
    run.waitReader();
}

} // namespace
int main() {
    try {
        rt_audit::reset();
        transport();
        readerAndSeek();
        nonfiniteMedia();
        const auto a = rt_audit::counts;
        check(!a.cppAllocate && !a.cppFree && !a.cAllocate && !a.cFree && !a.blockingLock,
              "Playback callback allocation/free/lock");
        std::cout
            << "{\"checks\":" << checks
            << ",\"live_offline_difference\":0,\"timeline_frames\":52750,\"seek_generation_"
               "retired\":true,\"rt_allocations\":0,\"rt_frees\":0,\"rt_blocking_locks\":0}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
