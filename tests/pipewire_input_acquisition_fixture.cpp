// SPDX-License-Identifier: GPL-3.0-only
// Test-only: requires an explicitly owned private PipeWire server.
#include <soundcurrent/pipewire_recording.hpp>
#include "native_port_handoff.hpp"
#include "rt_audit.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
using nlohmann::json;
struct Audit {
    std::atomic<std::uint64_t> allocations{0}, frees{0}, locks{0}, calls{0};
    static void begin(void *) noexcept { rt_audit::reset(); rt_audit::active = true; }
    static void end(void *p) noexcept {
        rt_audit::active = false;
        auto &a = *static_cast<Audit *>(p);
        const auto c = rt_audit::counts;
        a.allocations.fetch_add(c.cppAllocate + c.cAllocate, std::memory_order_relaxed);
        a.frees.fetch_add(c.cppFree + c.cFree, std::memory_order_relaxed);
        a.locks.fetch_add(c.blockingLock, std::memory_order_relaxed);
        a.calls.fetch_add(1, std::memory_order_relaxed);
    }
};
int main(int argc, char **argv) {
    try {
        if (argc != 4) throw std::runtime_error("Supply new owned project directory and owned input node/port");
        const auto root=utf8Path(argv[1]);
        const std::string node=argv[2], port=argv[3];
        if (!((node=="sc-preview-wav-player" && port=="output_MONO") ||
              (node=="sc-preview-source" && port=="capture_MONO")))
            throw std::runtime_error("Unowned input refused");
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("Project exists");
        auto s=makeOneTrackSession("Input acquisition observation", "Owned nonperiodic WAV");
        ProjectStore(root).save(s);
        RecordingSpec spec;
        spec.projectId=s.id;spec.trackId=s.tracks.front().id;
        spec.capture.maximumCallbackFrames=2048;
        PipeWireRecordingOptions options;
        options.bridge.maximumFrames=2048;options.bridge.stopAfterFrames=96000;
        Audit audit; options.audit={&audit, Audit::begin, Audit::end};
        PipeWireRecording recording(root,s,spec,options);
        std::vector<PipeWirePort> selected;
        for (const auto &p:recording.ports())
            if (!p.input && p.nodeName==node && p.portName==port)
                selected.push_back(p);
        if (selected.size()!=1) throw std::runtime_error("Owned player port missing");
        recording.connectInputs(selected);
        recording.activate();
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while ((recording.status()==AudioBridgeStatus::Ready || recording.status()==AudioBridgeStatus::Running)
               && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        recording.stop();
        const auto &r=recording.result();
        const auto origin=recording.timingOrigin();
        json report{{"format","sc-owned-input-acquisition-observation"},{"schemaMajor",1},
            {"status",unsigned(recording.status())},{"capturedFrames",recording.capturedFrames()},
            {"writtenFrames",recording.writtenFrames()},{"endReason",unsigned(recording.endReason())},
            {"rejectedFrames",recording.rejectedFrames()},{"assetPath",r.asset.relativePath},
            {"assetSha256",r.asset.sha256},{"firstFaultPresent",recording.firstFault().has_value()},
            {"storageDiagnostic",recording.faultStorageDiagnostic()},
            {"audit",{{"calls",audit.calls.load()},{"allocations",audit.allocations.load()},
                      {"frees",audit.frees.load()},{"locks",audit.locks.load()}}}};
        if (origin) report["timingOrigin"]={{"graphPosition",origin->devicePosition},
            {"monotonicNs",origin->monotonicNs},{"clockId",origin->clockId},
            {"cycle",origin->cycle},{"driverDelay",origin->driverDelay}};
        native_fixture::writePortHandoffs(root / "handoffs.json");
        std::ofstream out(root / "probe.json");out<<report.dump(2)<<'\n';
        if (!out) throw std::runtime_error("Cannot persist probe report");
        std::cout<<report.dump(2)<<'\n';
        return !recording.firstFault() && r.asset.frames==96000 && !audit.allocations &&
            !audit.frees && !audit.locks ? 0 : 2;
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}
