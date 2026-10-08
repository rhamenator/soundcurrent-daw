// SPDX-License-Identifier: GPL-3.0-only
// Run only on an explicitly owned private PipeWire server, never physical devices.
#include <soundcurrent/pipewire_recording.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>
using namespace soundcurrent::daw;
using nlohmann::json;
json clockJson(const DeviceBlockClock &c) {
    return {{"position",c.position},{"duration",c.duration},{"monotonicNs",c.monotonicNs},
            {"id",c.id},{"cycle",c.cycle},{"rateNumerator",c.rateNumerator},
            {"rateDenominator",c.rateDenominator},{"delay",c.delay},
            {"xrun",c.xrun},{"discontinuity",c.discontinuity}};
}
int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("Supply a new owned project directory");
        const auto root=utf8Path(argv[1]);
        if (!std::filesystem::create_directory(root))
            throw std::runtime_error("Owned project directory already exists");
        auto s=makeOneTrackSession("Owned clock fault probe", "Private audiotestsrc input");
        ProjectStore(root).save(s);
        RecordingSpec spec;
        spec.projectId=s.id;
        spec.trackId=s.tracks.front().id;
        spec.capture.maximumCallbackFrames=2048;
        PipeWireRecordingOptions options;
        options.bridge.maximumFrames=2048;
        options.bridge.stopAfterFrames=48000;
        PipeWireRecording recording(root,s,spec,options);
        std::vector<PipeWirePort> selected;
        for (const auto &port : recording.ports())
            if (!port.input && port.nodeName=="sc-preview-source" && port.portName=="capture_MONO")
                selected.push_back(port);
        if (selected.size()!=1) throw std::runtime_error("Explicit owned mono source not found");
        recording.connectInputs(selected);
        recording.activate();
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while ((recording.status()==AudioBridgeStatus::Ready ||
                recording.status()==AudioBridgeStatus::Running) &&
               std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        recording.stop();
        const auto fault=recording.firstFault();
        const auto &take=recording.result();
        json report{{"format","sc-owned-recording-fault-probe"},{"schemaMajor",1},
                    {"status",static_cast<unsigned>(recording.status())},
                    {"capturedFrames",recording.capturedFrames()},
                    {"writtenFrames",recording.writtenFrames()},
                    {"endReason",static_cast<unsigned>(recording.endReason())},
                    {"assetPath",take.asset.relativePath},{"assetSha256",take.asset.sha256},
                    {"firstFault",nullptr}};
        if (fault) report["firstFault"]={
            {"status",static_cast<unsigned>(fault->status)},
            {"reason",static_cast<unsigned>(fault->reason)},
            {"rejected",clockJson(fault->rejected)},{"previous",clockJson(fault->previous)},
            {"engineFrame",fault->engineFrame},{"capturedFrames",fault->capturedFrames},
            {"inputChannels",fault->inputChannels},{"outputChannels",fault->outputChannels},
            {"generation",fault->generation},{"bufferFrames",fault->bufferFrames},
            {"maximumFrames",fault->maximumFrames},{"expectedRate",fault->expectedRate},
            {"expectedChannels",fault->expectedChannels},
            {"callbackClock",fault->callbackClock},{"previousClock",fault->previousClock}};
        const bool expected=fault && fault->reason==AudioBridgeFaultReason::PositionJump &&
            fault->status==AudioBridgeStatus::ClockDiscontinuity && fault->previousClock &&
            fault->previous.position+fault->previous.duration!=fault->rejected.position &&
            take.asset.frames>0;
        report["expectedPositionJump"]=expected;
        std::cout<<report.dump(2)<<'\n';
        return expected ? 0 : 2;
    } catch (const std::exception &e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
