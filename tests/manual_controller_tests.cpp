// SPDX-License-Identifier: GPL-3.0-only
#include "manual_recording_controller.hpp"
#include "rt_audit.hpp"
#include <sndfile.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <thread>

using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void until(F predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < end, "Manual controller wait exceeded10s");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Directory {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("sc-manual-control-" + Id::generate().str());
    bool success = false;
    Directory() {
        std::filesystem::create_directory(root);
        std::cerr << "Owned manual controller project: " << root << '\n';
    }
    ~Directory() {
        if (success) {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    }
};
float sample(Frame frame) {
    return float(frame % 97 - 48) * .0625f;
}
struct Counters {
    std::atomic<unsigned> constructed{0}, activated{0}, destroyed{0}, steps{0}, callbacks{0};
    std::atomic<bool> wrongThread{false}, rtViolation{false}, audioStopped{false};
    std::atomic<bool> holdFactory{false}, factoryEntered{false}, failFactory{false};
    std::atomic<bool> holdStop{false}, stopEntered{false}, badActivation{false};
    std::atomic<bool> holdService{false}, serviceEntered{false};
    std::atomic<std::shared_ptr<ManualRecordingInterrupt>> token;
};
class Endpoint final : public ManualControlEndpoint {
    std::shared_ptr<Counters> counts;
    std::shared_ptr<ManualRecordingInterrupt> token;
    ManualRecordingRun run;
    std::thread::id owner = std::this_thread::get_id();
    std::thread audio;
    std::atomic<bool> joinAudio{false};
    bool stopped = false;
    void checkOwner() noexcept {
        if (std::this_thread::get_id() != owner)
            counts->wrongThread = true;
    }

  public:
    Endpoint(const ManualControlPreparation &p, std::shared_ptr<Counters> c)
        : counts(std::move(c)), token(p.options.run.interrupt),
          run(p.root, *p.session, p.plan, p.arms, p.options.run) {
        ++counts->constructed;
    }
    ~Endpoint() override {
        checkOwner();
        stop(false);
        ++counts->destroyed;
    }
    std::vector<PipeWirePort> ports() override {
        checkOwner();
        return {};
    }
    void activate(const std::vector<PipeWirePort> &, const std::vector<PipeWirePort> &) override {
        checkOwner();
        check(!counts->badActivation && !token->stopRequested(), "Deliberate activation refusal");
        ++counts->activated;
        audio = std::thread([this] {
            std::array<float, 256> input{}, output{};
            const float *in = input.data();
            float *out = output.data();
            while (!joinAudio.load()) {
                unsigned pending = counts->steps.load();
                if (!token->stopRequested() &&
                    (!pending || !counts->steps.compare_exchange_strong(pending, pending - 1))) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                const auto start = run.position();
                for (unsigned n = 0; n < 256; ++n)
                    input[n] = sample(start + n);
                DuplexStatus status;
                rt_audit::reset();
                {
                    rt_audit::Guard guard;
                    status = run.process({100000 + std::uint64_t(start), 256,
                                          10000 + std::uint64_t(start) * 1000000000 / 48000, 17,
                                          std::uint32_t(1 + start / 256), 1, 48000},
                                         {&in, 1}, {&out, 1}, 256);
                }
                const auto audit = rt_audit::counts;
                if (audit.cppAllocate + audit.cppFree + audit.cAllocate + audit.cFree +
                    audit.blockingLock)
                    counts->rtViolation = true;
                ++counts->callbacks;
                if (status == DuplexStatus::Stopped) {
                    counts->audioStopped = true;
                    return;
                }
                if (status != DuplexStatus::Running) {
                    counts->rtViolation = true;
                    return;
                }
            }
        });
    }
    std::uint64_t prepareTake() override {
        checkOwner();
        return run.prepareTake();
    }
    void abandonTake(std::uint64_t id) override {
        checkOwner();
        run.abandonTake(id);
    }
    ManualPunchSubmit submit(ManualPunchCommand c) noexcept override {
        checkOwner();
        return run.submit(c);
    }
    void service() override {
        checkOwner();
        if (run.position() && counts->holdService) {
            counts->serviceEntered = true;
            until([&] { return !counts->holdService.load(); });
        }
        run.service();
    }
    bool acknowledgement(ManualPunchReceipt &r) noexcept override {
        checkOwner();
        return run.acknowledgement(r);
    }
    bool takeGroup(ManualRecordedGroup &g) override {
        checkOwner();
        return run.takeGroup(g);
    }
    void stop(bool cancel) override {
        checkOwner();
        if (!stopped) {
            counts->stopEntered = true;
            until([&] { return !counts->holdStop.load(); });
            joinAudio = true;
            if (audio.joinable())
                audio.join();
            stopped = true;
        }
        cancel ? run.cancel() : run.stop();
    }
    void checkError() override {
        checkOwner();
        run.checkError();
    }
    void checkReader() override {
        checkOwner();
        run.checkReader();
    }
    DuplexStatus status() noexcept override {
        checkOwner();
        return run.status();
    }
    Frame position() noexcept override {
        checkOwner();
        return run.position();
    }
    std::size_t occupiedSlots() noexcept override {
        checkOwner();
        return run.occupiedSlots();
    }
};
struct Fixture {
    Directory directory;
    Session session = makeOneTrackSession("Manual control — Ελληνικά", "Lane0");
    std::shared_ptr<Counters> counts = std::make_shared<Counters>();
    ManualControlOptions options() {
        return {[c = counts](const ManualControlPreparation &p) {
            c->token = p.options.run.interrupt;
            c->factoryEntered = true;
            until([&] { return !c->holdFactory.load(); });
            check(!c->failFactory, "Deliberate factory refusal");
            return std::make_unique<Endpoint>(p, c);
        }};
    }
    Fixture() {
        session.tracks.front().eq.bands[0].gainDb = 4;
        session.tracks.push_back(makeAudioTrack("Lane1", {}, session.sampleRate));
        ProjectStore(directory.root).save(session);
    }
    ManualControlCommand prepare() {
        ManualControlCommand command;
        auto &p = command.preparation;
        p.root = directory.root;
        p.session = std::make_shared<const Session>(session);
        p.modelRevision = 1;
        for (const auto &track : session.tracks) {
            p.plan.tracks.push_back({track.id, {{0, 0, .25}}});
            p.arms.push_back({{track.id, {0}, 0, RecordingMonitor::PostEq}, {}});
        }
        p.options.run.playback.graph.maximumFrames = 256;
        p.options.run.playback.endFrame = 20000;
        p.options.run.playback.slabFrames = 512;
        p.options.run.capture.maximumCallbackFrames = p.options.run.capture.slabFrames = 256;
        p.options.run.backend = CaptureBackend::Synthetic;
        return command;
    }
    void verify(const ManualRecordedGroup &group, bool canceled = false) {
        check(group.canceled == canceled && group.complete() == !canceled &&
                  group.lanes.size() == 2 && group.endFrame - group.beginFrame == 87,
              "Controller group boundary/classification differs");
        for (const auto &lane : group.lanes) {
            check(lane.checkpoint && lane.checkpoint->committedFrames == 87 &&
                      lane.capturedFrames == 87 && !lane.verificationError,
                  "Controller lost durable raw extent");
            SF_INFO info{};
#ifdef _WIN32
            auto *file = sf_wchar_open(lane.checkpoint->source.c_str(), SFM_READ, &info);
#else
            auto *file = sf_open(lane.checkpoint->source.c_str(), SFM_READ, &info);
#endif
            check(file && info.frames == 87 && info.channels == 1, "Controller raw header differs");
            std::array<float, 87> samples{};
            const auto read = sf_readf_float(file, samples.data(), 87);
            sf_close(file);
            check(read == 87, "Controller raw prefix truncated");
            for (unsigned n = 0; n < 87; ++n)
                check(samples[n] == sample(group.beginFrame + n), "Controller raw sample differs");
        }
    }
    ~Fixture() {
        counts->holdFactory = false;
        counts->holdStop = false;
    }
};
ManualControlReceipt send(ManualRecordingController &controller, ManualControlCommand command) {
    const auto submitted = controller.submit(std::move(command));
    check(submitted.admission == ManualControlAdmission::Accepted, "Controller admission refused");
    std::optional<ManualControlReceipt> result;
    until([&] {
        for (const auto &r : controller.snapshot()->commands)
            if (r.sequence == submitted.sequence) {
                result = r;
                return true;
            }
        return false;
    });
    check(controller.acknowledgeCommand(submitted.sequence) &&
              !controller.acknowledgeCommand(submitted.sequence),
          "Controller receipt acknowledgment lost or duplicated");
    until([&] {
        const auto view = controller.snapshot();
        return std::none_of(view->commands.begin(), view->commands.end(),
                            [&](const auto &r) { return r.sequence == submitted.sequence; });
    });
    return *result;
}
ManualControlCommand action(ManualControlKind kind, std::uint64_t generation) {
    ManualControlCommand command;
    command.kind = kind;
    command.generation = generation;
    return command;
}
void close(ManualRecordingController &controller, const std::shared_ptr<Counters> &counts) {
    controller.requestShutdown();
    until([&] { return controller.snapshot()->closed; });
    check(counts->constructed == counts->destroyed && !counts->wrongThread && !counts->rtViolation,
          "Controller close leaked endpoint or violated ownership/RT");
    check(controller.submit({}).admission == ManualControlAdmission::Closing,
          "Closed controller accepted another command");
}
void repeatedTakes() {
    Fixture f;
    ManualRecordingController controller(f.options());
    const auto prepared = send(controller, f.prepare());
    check(prepared.result == ManualControlResult::Applied && prepared.generation &&
              !std::filesystem::exists(f.directory.root / "media"),
          "Controller prepare created jobs or lost generation");
    const auto generation = prepared.generation;
    check(send(controller, action(ManualControlKind::Activate, generation)).result ==
              ManualControlResult::Applied,
          "Controller activation failed");
    auto bad = action(ManualControlKind::Activate, generation + 1);
    check(send(controller, bad).result == ManualControlResult::Rejected && f.counts->activated == 1,
          "Stale activation disturbed current transport");
    check(send(controller, f.prepare()).result == ManualControlResult::Rejected &&
              controller.snapshot()->phase == ManualControlPhase::Playing,
          "Rejected preparation stopped live transport");
    for (unsigned take = 0; take < 8; ++take) {
        const auto reserved = send(controller, action(ManualControlKind::PrepareTake, generation));
        check(reserved.result == ManualControlResult::Applied && reserved.take,
              "Controller did not prepare a repeated take");
        auto in = action(ManualControlKind::Punch, generation);
        in.frame = take * 256 + 13;
        in.take = reserved.take;
        auto out = in;
        out.action = ManualPunchAction::Out;
        out.frame = take * 256 + 100;
        out.take = 0;
        const auto inReceipt = send(controller, in);
        const auto outReceipt = send(controller, out);
        check(inReceipt.result == ManualControlResult::Applied &&
                  outReceipt.result == ManualControlResult::Applied &&
                  controller.snapshot()->punches.size() == take * 2,
              "Controller faked audio application at command admission");
        ++f.counts->steps;
        until([&] { return controller.snapshot()->groups.size() == take + 1; });
        const auto view = controller.snapshot();
        check(view->position == Frame((take + 1) * 256) && view->punches.size() == (take + 1) * 2 &&
                  view->punches[take * 2].command.revision == inReceipt.sequence &&
                  view->punches[take * 2].result == ManualPunchResult::Applied &&
                  view->punches[take * 2 + 1].command.revision == outReceipt.sequence &&
                  view->punches[take * 2 + 1].result == ManualPunchResult::Applied,
              "Controller lost reliable original audio receipts");
        f.verify(*view->groups.back().group);
    }
    const auto original = controller.snapshot();
    check(send(controller, action(ManualControlKind::PrepareTake, generation)).result ==
              ManualControlResult::Rejected,
          "Controller exceeded eight unconsumed result groups");
    auto candidate = f.session;
    for (const auto &group : original->groups)
        candidate = withManualRecording(candidate, *group.group);
    auto canonical = f.session;
    EditHistory history(canonical);
    check(history.adopt(candidate) && history.undo() && canonical == f.session && history.redo() &&
              canonical == candidate,
          "Controller groups cannot be adopted with grouped history");
    check(ProjectStore(f.directory.root).load() == f.session && f.counts->constructed == 1 &&
              f.counts->activated == 1,
          "Controller receipt consumption mutated saved project or reset graph");
    ProjectStore(f.directory.root).save(candidate);
    check(ProjectStore(f.directory.root).load() == candidate,
          "Controller group Save/reopen differs");
    for (const auto &r : original->punches)
        check(controller.acknowledgePunch(r.command.revision),
              "Controller lost punch acknowledgement");
    for (const auto &g : original->groups)
        check(controller.acknowledgeGroup(g.generation, g.group->take) &&
                  !controller.acknowledgeGroup(g.generation, g.group->take),
              "Controller lost or duplicated group acknowledgement");
    until([&] {
        return controller.snapshot()->groups.empty() && controller.snapshot()->punches.empty();
    });
    const auto unused = send(controller, action(ManualControlKind::PrepareTake, generation));
    check(unused.result == ManualControlResult::Applied && unused.take,
          "Consuming result groups did not replenish preparation");
    auto abandon = action(ManualControlKind::AbandonTake, generation);
    abandon.take = unused.take;
    check(send(controller, abandon).result == ManualControlResult::Applied,
          "Controller cannot abandon an unused preparation");
    const auto stopped = controller.requestStop();
    until([&] { return controller.snapshot()->stopAcknowledged == stopped; });
    close(controller, f.counts);
    f.directory.success = true;
    std::cout << "Controller repeated takes: original raw, audio replies, history, Save/reopen, "
                 "one graph\n";
}
void saturatedPreparation() {
    Fixture f;
    f.counts->holdFactory = true;
    ManualRecordingController controller(f.options());
    struct Release {
        std::shared_ptr<Counters> c;
        ~Release() {
            c->holdFactory = false;
            c->holdStop = false;
        }
    } release{f.counts};
    check(controller.submit(f.prepare()).admission == ManualControlAdmission::Accepted,
          "Held prepare refused");
    until([&] { return f.counts->factoryEntered.load(); });
    for (unsigned n = 0; n < 16; ++n)
        check(controller.submit(action(ManualControlKind::PrepareTake, 1)).admission ==
                  ManualControlAdmission::Accepted,
              "Bounded queue filled early");
    check(controller.submit({}).admission == ManualControlAdmission::Full,
          "Controller queue admission is unbounded");
    const auto stopped = controller.requestStop(true);
    check(f.counts->token.load()->cancelRequested() && !controller.snapshot()->stopAcknowledged,
          "Full queue delayed priority signal or fabricated join acknowledgement");
    f.counts->holdFactory = false;
    until([&] {
        return controller.snapshot()->commands.size() == 17 &&
               controller.snapshot()->stopAcknowledged == stopped;
    });
    for (const auto &r : controller.snapshot()->commands)
        check(r.result == ManualControlResult::Stopped, "Canceled prefix executed queued work");
    check(f.counts->activated == 0 && !std::filesystem::exists(f.directory.root / "media") &&
              ProjectStore(f.directory.root).load() == f.session,
          "Canceled preparation activated routes/jobs or changed canonical state");
    close(controller, f.counts);
    f.directory.success = true;
    std::cout << "Controller saturation:16 queued, immediate Cancel, all17 terminal receipts\n";
}
void replyAndResultPressure() {
    Fixture f;
    ManualRecordingController controller(f.options());
    const auto generation = send(controller, f.prepare()).generation;
    for (unsigned n = 0; n < 8; ++n)
        check(send(controller, action(ManualControlKind::PrepareTake, generation)).take,
              "Prepared slot pressure refused early");
    check(send(controller, action(ManualControlKind::PrepareTake, generation)).result ==
              ManualControlResult::Rejected,
          "Prepared/result admission exceeded eight slots");
    for (unsigned n = 0; n < 64; ++n) {
        auto out = action(ManualControlKind::Punch, generation);
        out.action = ManualPunchAction::Out;
        check(send(controller, out).result == ManualControlResult::Applied,
              "Reliable punch admission refused early");
    }
    auto out = action(ManualControlKind::Punch, generation);
    out.action = ManualPunchAction::Out;
    check(send(controller, out).result == ManualControlResult::Rejected,
          "Reliable punch admission exceeded retained capacity");
    check(send(controller, action(ManualControlKind::Activate, generation)).result ==
              ManualControlResult::Applied,
          "Reply-pressure activation failed");
    ++f.counts->steps;
    until([&] { return controller.snapshot()->punches.size() == 64; });
    for (const auto &r : controller.snapshot()->punches)
        check(r.result == ManualPunchResult::NotRecording,
              "Reply pressure dropped or fabricated callback result");
    check(send(controller, out).result == ManualControlResult::Rejected,
          "Backend-to-controller transfer incorrectly returned application reply capacity");
    const auto first = controller.snapshot()->punches.front().command.revision;
    check(controller.acknowledgePunch(first) && !controller.acknowledgePunch(first),
          "Reply consumption duplicated");
    until([&] { return controller.snapshot()->punches.size() == 63; });
    check(send(controller, out).result == ManualControlResult::Applied,
          "Explicit reply consumption did not replenish admission");
    close(controller, f.counts);
    check(controller.snapshot()->punches.size() == 64 && controller.snapshot()->groups.empty(),
          "Close dropped reliable replies or fabricated unused-slot results");
    const auto closedReceipt = controller.snapshot()->punches.front().command.revision;
    check(controller.acknowledgePunch(closedReceipt) &&
              !controller.acknowledgePunch(closedReceipt) &&
              controller.snapshot()->punches.size() == 63,
          "Closed controller cannot consume retained punch receipts");
    f.directory.success = true;
    std::cout << "Controller pressure:8 slots,64 retained replies, explicit replenishment, closed "
                 "receipts\n";
}
void failedPreparation() {
    Fixture f;
    f.counts->failFactory = true;
    ManualRecordingController controller(f.options());
    const auto failed = send(controller, f.prepare());
    check(failed.result == ManualControlResult::Rejected && controller.snapshot()->fault &&
              controller.snapshot()->phase == ManualControlPhase::Idle,
          "Factory failure left a stuck preparation");
    f.counts->failFactory = false;
    const auto generation = send(controller, f.prepare()).generation;
    f.counts->badActivation = true;
    check(send(controller, action(ManualControlKind::Activate, generation)).result ==
                  ManualControlResult::Rejected &&
              controller.snapshot()->fault &&
              controller.snapshot()->phase == ManualControlPhase::Idle && f.counts->destroyed == 1,
          "Activation failure retained half-configured endpoint");
    close(controller, f.counts);
    f.directory.success = true;
}
void cancelDuringFinalization() {
    Fixture f;
    ManualRecordingController controller(f.options());
    struct Release {
        std::shared_ptr<Counters> c;
        ~Release() {
            c->holdStop = false;
            c->holdService = false;
        }
    } release{f.counts};
    const auto generation = send(controller, f.prepare()).generation;
    const auto take = send(controller, action(ManualControlKind::PrepareTake, generation)).take;
    send(controller, action(ManualControlKind::Activate, generation));
    auto punch = action(ManualControlKind::Punch, generation);
    punch.take = take;
    punch.frame = 13;
    send(controller, punch);
    punch.take = 0;
    punch.action = ManualPunchAction::Out;
    punch.frame = 100;
    send(controller, punch);
    f.counts->holdService = true;
    f.counts->holdStop = true;
    ++f.counts->steps;
    until([&] { return f.counts->serviceEntered.load(); });
    const auto stop = controller.requestStop();
    until([&] { return f.counts->audioStopped.load(); });
    check(controller.snapshot()->stopAcknowledged < stop,
          "Controller acknowledged Stop before native/control joins");
    f.counts->holdService = false;
    until([&] { return f.counts->stopEntered.load(); });
    check(controller.snapshot()->phase == ManualControlPhase::Finalizing &&
              controller.snapshot()->groups.empty(),
          "Controller published a result before held finalization completed");
    const auto cancel = controller.requestStop(true);
    check(f.counts->token.load()->cancelRequested() &&
              controller.snapshot()->stopAcknowledged < cancel,
          "Cancel during Stop was lost or faked completion");
    controller.requestShutdown();
    check(!controller.snapshot()->closed, "Controller closed before endpoint join");
    f.counts->holdStop = false;
    until([&] { return controller.snapshot()->closed; });
    const auto view = controller.snapshot();
    check(view->groups.size() == 1 && view->punches.size() == 2 &&
              f.counts->constructed == f.counts->destroyed && !f.counts->wrongThread &&
              !f.counts->rtViolation,
          "Late controller Cancel lost results/replies or leaked endpoint");
    f.verify(*view->groups.front().group, true);
    bool refused = false;
    try {
        withManualRecording(f.session, *view->groups.front().group, true);
    } catch (const ProjectError &) {
        refused = true;
    }
    check(refused && ProjectStore(f.directory.root).load() == f.session,
          "Canceled controller group was adopted or auto-saved");
    check(controller.acknowledgeGroup(generation, view->groups.front().group->take) &&
              controller.snapshot()->groups.empty() && view->groups.size() == 1,
          "Closed group acknowledgment lost or mutated an older immutable snapshot");
    f.directory.success = true;
    std::cout << "Controller Stop-to-Cancel during held finalization: next callback stopped, "
                 "joined before close, raw retained\n";
}
void controlReceiptPressure() {
    Fixture f;
    ManualRecordingController controller(f.options());
    for (unsigned n = 0; n < 64; ++n) {
        check(controller.submit(action(ManualControlKind::Activate, 999)).admission ==
                  ManualControlAdmission::Accepted,
              "Control receipt capacity refused early");
        until([&] { return controller.snapshot()->commands.size() == n + 1; });
    }
    check(controller.submit({}).admission == ManualControlAdmission::Full,
          "Control receipt capacity returned credit before explicit consumption");
    const auto sequence = controller.snapshot()->commands.front().sequence;
    check(controller.acknowledgeCommand(sequence), "Control receipt credit cannot be released");
    until([&] { return controller.snapshot()->commands.size() == 63; });
    check(controller.submit(action(ManualControlKind::Activate, 999)).admission ==
              ManualControlAdmission::Accepted,
          "Control receipt consumption did not replenish credit");
    until([&] { return controller.snapshot()->commands.size() == 64; });
    close(controller, f.counts);
    check(controller.snapshot()->commands.size() == 64,
          "Close silently discarded unresolved control receipts");
    check(controller.acknowledgeCommand(controller.snapshot()->commands.front().sequence) &&
              controller.snapshot()->commands.size() == 63,
          "Closed controller cannot consume retained control receipts");
    f.directory.success = true;
}
} // namespace
int main() {
    try {
        repeatedTakes();
        saturatedPreparation();
        replyAndResultPressure();
        failedPreparation();
        cancelDuringFinalization();
        controlReceiptPressure();
    } catch (const std::exception &e) {
        std::cerr << "Manual controller failure: " << e.what() << '\n';
        return 1;
    }
}
