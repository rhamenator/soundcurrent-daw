// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/manual_recording.hpp>
#include "rt_audit.hpp"
#include <sndfile.h>
#include <chrono>
#include <iostream>
#include <thread>

using namespace soundcurrent::daw;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Directory {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sc-manual-interrupt-" + Id::generate().str());
    bool success = false;
    Directory() {
        std::filesystem::create_directory(root);
        std::cerr << "Owned interruption project: " << root << '\n';
    }
    ~Directory() {
        if (success) {
            std::error_code e;
            std::filesystem::remove_all(root, e);
        }
    }
};
float signal(Frame f, unsigned channel) {
    return float(double((f + channel * 13) % 97) - 48) * .0625f;
}
Frame latency(unsigned channel) {
    constexpr Frame delays[]{0, 41, 200, 4097};
    return delays[channel % 4];
}
template <class F> void until(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, "Owned interruption wait exceeded5s");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void beforeFirstCallback(bool cancel) {
    Directory d;
    auto s = makeOneTrackSession("Queued priority — Ελληνικά", "Unstarted");
    ProjectStore(d.root).save(s);
    auto interrupt = std::make_shared<ManualRecordingInterrupt>();
    ManualRecordingOptions o;
    o.playback.graph.maximumFrames = 256;
    o.playback.graph.generation = 7;
    o.playback.endFrame = 20000;
    o.playback.slabFrames = 512;
    o.capture.maximumCallbackFrames = 256;
    o.capture.slabFrames = 256;
    o.interrupt = interrupt;
    o.backend = CaptureBackend::Synthetic;
    ManualRecordingRun run(d.root, s, {{}, {{s.tracks[0].id, {{0, 0, 1}}}}},
                           {{{s.tracks[0].id, {0}, 0, RecordingMonitor::Off}, {}}}, o);
    const auto id = run.prepareTake();
    check(run.submit({ManualPunchAction::In, 31, 7, 1, id}) == ManualPunchSubmit::Accepted,
          "Unstarted In refused");
    check(run.submit({ManualPunchAction::Out, 300, 7, 2, 0}) == ManualPunchSubmit::Accepted,
          "Unstarted Out refused");
    interrupt->requestStop();
    if (cancel)
        interrupt->requestCancel(); // Monotonic Stop-to-Cancel escalation.
    check(run.status() == DuplexStatus::Ready, "Priority signal faked callback acknowledgement");
    check(run.submit({ManualPunchAction::In, -1, 7, 3, id}) == ManualPunchSubmit::Stopped,
          "Unobserved priority signal admitted a command");
    bool refused = false;
    try {
        run.prepareTake();
    } catch (const ProjectError &) {
        refused = true;
    }
    check(refused, "Unobserved priority signal admitted a pool");
    std::array<float, 256> output;
    output.fill(1);
    float *out = output.data();
    rt_audit::reset();
    DuplexStatus status;
    {
        rt_audit::Guard guard;
        status = run.process({10000, 256, 10000, 17, 1, 1, 48000}, {}, {&out, 1}, 256);
    }
    const auto audit = rt_audit::counts;
    check(status == DuplexStatus::Stopped && !run.position() && !audit.cppAllocate &&
              !audit.cppFree && !audit.cAllocate && !audit.cFree && !audit.blockingLock,
          "Unstarted priority signal advanced playback or violated RT");
    for (auto value : output)
        check(value == 0, "Unstarted priority signal did not silence output");
    run.stop();
    ManualPunchReceipt reply;
    for (unsigned revision = 1; revision <= 2; ++revision)
        check(run.acknowledgement(reply) && reply.command.revision == revision &&
                  reply.result == ManualPunchResult::TransportStopped,
              "Unstarted queued command lost terminal reply");
    ManualRecordedGroup group;
    check(!run.acknowledgement(reply) && !run.takeGroup(group) && !run.occupiedSlots(),
          "Unstarted priority signal fabricated a result or retained resources");
    check(ProjectStore(d.root).load() == s && interrupt->cancelRequested() == cancel,
          "Unstarted priority signal changed saved state or lost escalation");
    run.checkError();
    run.checkReader();
    d.success = true;
}
void blockedStartup(bool cancel) {
    Directory d;
    auto session = makeOneTrackSession("Interruption — Українська", "Lane0");
    while (session.tracks.size() < 32)
        session.tracks.push_back(makeAudioTrack("Armed", {}, session.sampleRate));
    ProjectStore(d.root).save(session);
    MixPlan plan;
    std::vector<ManualRecordingArm> arms;
    std::atomic<bool> entered{false}, release{false};
    std::atomic<unsigned> constructionCallbacks{0};
    const auto interrupt = std::make_shared<ManualRecordingInterrupt>();
    for (unsigned n = 0; n < 32; ++n) {
        plan.tracks.push_back({session.tracks[n].id, {{0, 0, .015625}}});
        RecordingOptions writer;
        writer.checkpointFrames = 256;
        writer.boundary = [&, n](RecordingBoundary boundary, Frame frames) {
            if (boundary != RecordingBoundary::BeforeJournalPublish || frames)
                return;
            ++constructionCallbacks;
            if (!n) {
                entered.store(true, std::memory_order_release);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!release.load(std::memory_order_acquire)) {
                    if (std::chrono::steady_clock::now() >= deadline)
                        throw std::runtime_error("Owned disk construction gate exceeded5s");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        };
        arms.push_back({{session.tracks[n].id, {n}, latency(n), RecordingMonitor::PostEq}, writer});
    }
    ManualRecordingOptions o;
    o.playback.graph.maximumFrames = 256;
    o.playback.graph.generation = 7;
    o.playback.endFrame = 20000;
    o.playback.slabFrames = 512;
    o.capture.maximumCallbackFrames = 256;
    o.capture.slabFrames = 256;
    o.nativeInputs = 32;
    o.backend = CaptureBackend::Synthetic;
    o.interrupt = interrupt;
    ManualRecordingRun run(d.root, session, plan, arms, o);
    const auto take = run.prepareTake();
    check(run.submit({ManualPunchAction::In, 13, 7, 1, take}) == ManualPunchSubmit::Accepted,
          "In refused before priority signal");
    check(run.submit({ManualPunchAction::Out, 10000, 7, 2, 0}) == ManualPunchSubmit::Accepted,
          "Future Out refused before priority signal");
    std::array<std::array<float, 256>, 32> input{};
    std::array<const float *, 32> views{};
    std::array<float, 256> output{};
    float *out = output.data();
    for (unsigned n = 0; n < 32; ++n) {
        views[n] = input[n].data();
        for (unsigned f = 0; f < 256; ++f)
            input[n][f] = Frame(f) >= latency(n) ? signal(Frame(f) - latency(n), n) : 0;
    }
    DeviceBlockClock clock{100000, 256, 2000000, 17, 1, 1, 48000};
    rt_audit::reset();
    DuplexStatus first;
    {
        rt_audit::Guard guard;
        first = run.process(clock, views, {&out, 1}, 256);
    }
    check(first == DuplexStatus::Running && run.position() == 256, "Initial raw prefix failed");
    std::exception_ptr controlError;
    std::thread control([&] {
        try {
            run.service();
        } catch (...) {
            controlError = std::current_exception();
        }
    });
    struct Join {
        std::atomic<bool> &release;
        std::thread &thread;
        ~Join() {
            release.store(true, std::memory_order_release);
            if (thread.joinable())
                thread.join();
        }
    } join{release, control};
    until([&] { return entered.load(std::memory_order_acquire); });
    check(constructionCallbacks == 1 && !release, "Startup was not blocked in first real writer");
    if (cancel)
        interrupt->requestCancel();
    else
        interrupt->requestStop();
    clock.position += 256;
    clock.monotonicNs += 256000000000ULL / 48000;
    ++clock.cycle;
    DuplexStatus stopped;
    {
        rt_audit::Guard guard;
        stopped = run.process(clock, views, {&out, 1}, 256);
    }
    check(stopped == DuplexStatus::Stopped && run.position() == 256 && !release,
          "Priority signal waited for disk construction or advanced raw/mix");
    for (float f : output)
        check(f == 0, "Priority stop left audible output");
    const auto audit = rt_audit::counts;
    check(!(audit.cppAllocate + audit.cppFree + audit.cAllocate + audit.cFree + audit.blockingLock),
          "Priority callback allocated/freed/locked");
    // GUI never touches the run's serialized control methods while service is live.
    release.store(true, std::memory_order_release);
    control.join();
    if (controlError)
        std::rethrow_exception(controlError);
    check(constructionCallbacks == 1, "Interrupted service started more disk workers");
    check(run.submit({ManualPunchAction::In, -1, 7, 3, take}) == ManualPunchSubmit::Stopped,
          "Priority signal admitted a new command");
    bool refused = false;
    try {
        run.prepareTake();
    } catch (const ProjectError &) {
        refused = true;
    }
    check(refused, "Priority signal admitted a new prepared take");
    run.stop(); // Cancel token must be honored even through stop/destructor cleanup.
    run.checkReader();
    ManualPunchReceipt reply;
    check(run.acknowledgement(reply) && reply.result == ManualPunchResult::Applied &&
              reply.appliedFrame == 13,
          "Applied In reply lost during priority stop");
    check(run.acknowledgement(reply) && reply.result == ManualPunchResult::TransportStopped,
          "Future Out lost its reliable terminal reply");
    check(!run.acknowledgement(reply), "Duplicate priority reply");
    ManualRecordedGroup group;
    check(run.takeGroup(group) && group.lanes.size() == 32 && group.beginFrame == 13 &&
              group.endFrame == 256 && group.canceled == cancel && !group.complete(),
          "Priority result lost boundary/cancellation classification");
    unsigned verified = 0;
    for (unsigned n = 0; n < 32; ++n) {
        const auto &lane = group.lanes[n];
        const Frame extent = std::max<Frame>(0, 256 - 13 - latency(n));
        check(lane.capturedFrames == extent && !lane.verificationError,
              "Priority stop lost accepted raw extent or verification");
        check(!cancel || lane.outcome == ManualLaneOutcome::Canceled, "Cancel token became Stop");
        if (!extent) {
            check(!lane.job && !lane.origin && !lane.result, "Empty delayed lane fabricated media");
            continue;
        }
        check(lane.spec.capture.startFrame == 13 + latency(n) && lane.origin &&
                  lane.origin->devicePosition == 100000 + std::uint64_t(13 + latency(n)),
              "Priority origin was replaced by worker startup time");
        check(lane.checkpoint.has_value(), "Priority prefix lacks inspectable checkpoint");
        const auto durable = lane.checkpoint->committedFrames;
        if (!durable)
            continue;
        SF_INFO info{};
#ifdef _WIN32
        auto *file = sf_wchar_open(lane.checkpoint->source.c_str(), SFM_READ, &info);
#else
        auto *file = sf_open(lane.checkpoint->source.c_str(), SFM_READ, &info);
#endif
        if (!file || info.channels != 1 || info.samplerate != 48000)
            std::cerr << "Priority raw header lane=" << n << " source=" << lane.checkpoint->source
                      << " opened=" << bool(file) << " frames=" << info.frames
                      << " channels=" << info.channels << " rate=" << info.samplerate << '\n';
        check(file && info.channels == 1 && info.samplerate == 48000, "Priority raw header failed");
        std::vector<float> audio(std::size_t(durable), 0);
        const auto read = sf_readf_float(file, audio.data(), durable);
        sf_close(file);
        check(read == durable, "Priority durable prefix truncated");
        for (Frame f = 0; f < durable; ++f)
            check(audio[std::size_t(f)] == signal(13 + f, n),
                  "Priority raw sample shifted/changed");
        ++verified;
    }
    bool adoptionRefused = false;
    try {
        withManualRecording(session, group, cancel);
    } catch (const ProjectError &) {
        adoptionRefused = true;
    }
    check(adoptionRefused, "Canceled/incomplete group silently adopted");
    if (!cancel) {
        check(verified == 24, "Healthy priority stop did not drain every positive lane");
        run.checkError();
        const auto candidate = withManualRecording(session, group, true);
        check(candidate.assets.size() == 24 && ProjectStore(d.root).load() == session,
              "Explicit partial preview mutated canonical saved project");
    }
    check(!run.takeGroup(group) && !run.occupiedSlots(), "Priority result/pool was not reclaimed");
    // The external signal remains valid after endpoint destruction and never resets.
    check(interrupt->stopRequested() && interrupt->cancelRequested() == cancel,
          "Priority token reset or lost cancellation escalation");
    d.success = true;
    std::cout << "Priority " << (cancel ? "Cancel" : "Stop")
              << ": 32lanes, blocked real disk construction, next-boundary stop, verified="
              << verified << ", zeroRT, saved original retained\n";
}
} // namespace
int main() {
    try {
        beforeFirstCallback(false);
        beforeFirstCallback(true);
        blockedStartup(false);
        blockedStartup(true);
    } catch (const std::exception &e) {
        std::cerr << "Interruption failure: " << e.what() << '\n';
        return 1;
    }
}
