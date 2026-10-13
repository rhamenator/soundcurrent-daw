// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_monitor_recording.hpp"
#include "pipewire_monitor_recording.hpp"
#include <variant>
namespace soundcurrent::daw {
using MonitorControlDeclaration=std::variant<NativeOriginalRecordingSpec,PipeWireRecordingSpec>;
using MonitorControlOutcome=std::variant<NativeMonitorRecordingResult,PipeWireMonitorRecordingResult>;
// Common counts only. No native clocks or negotiated formats are projected here.
struct MonitorControlProgress {
    bool processingDone=false,diskDone=false,captureEnded=false,renderEnded=false;
    std::uint64_t rawAcceptedFrames=0,monitorAcceptedFrames=0,publishedMonitorFrames=0,
        deliveredMonitorFrames=0,copiedMonitorSourceFrames=0;
};
// ONE serialized off-RT worker. service/readyToJoin may join SDK owners; never
// call this interface on GUI/audio callbacks. Typed original backend facts stay
// separate. Result copying and object retirement happen after owner joins.
class PreparedMonitorControlEndpoint {
  public:
    virtual ~PreparedMonitorControlEndpoint()=default;
    virtual MonitorControlDeclaration declared() const=0;
    virtual MonitorOutputOrdinal first() const noexcept=0;
    virtual const std::filesystem::path &jobDirectory() const noexcept=0;
    virtual void activate()=0;
    virtual MonitorControlCode requestStopAt(MonitorOutputOrdinal) noexcept=0;
    virtual MonitorControlCode receiveStopReceipt(MonitorControlReceipt &) noexcept=0;
    virtual void requestCancel() noexcept=0;
    virtual MonitorControlProgress progress() const noexcept=0;
    virtual bool readyToJoin()=0;
    virtual bool failedOwnerReadyToCancel() const noexcept=0;
    virtual MonitorControlOutcome wait()=0;
    virtual MonitorControlOutcome cancelAndWait()=0;
    virtual EqEvent equalizerEvent(const EqSettings &,const ParameterAddress &,MonitorOutputOrdinal) const=0;
    virtual EqEvent equalizerEnableEvent(bool,MonitorOutputOrdinal) const=0;
    virtual SubmitStatus submitEqualizer(const EqEvent &,std::uint64_t) noexcept=0;
    virtual bool equalizerAcknowledgement(ImmediateAcknowledgement &) noexcept=0;
};
std::unique_ptr<PreparedMonitorControlEndpoint> prepareNativeMonitorControlEndpoint(
    std::filesystem::path,std::unique_ptr<NativeMonitorStreams>,NativeMonitorRecordingOptions);
std::unique_ptr<PreparedMonitorControlEndpoint> preparePipeWireMonitorControlEndpoint(
    std::filesystem::path,PipeWireMonitorStreamFactory,PipeWireMonitorRecordingOptions,
    const std::vector<AudioPort> &inputs,const std::vector<AudioPort> &outputs);
}
