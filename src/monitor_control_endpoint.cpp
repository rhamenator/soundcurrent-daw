// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/monitor_control_endpoint.hpp>
namespace soundcurrent::daw {
namespace {
class Native final:public PreparedMonitorControlEndpoint {
    ResourceLease lease_;NativeMonitorRecording owner_;
  public:
    Native(std::filesystem::path root,std::unique_ptr<NativeMonitorStreams> streams,NativeMonitorRecordingOptions o)
        :lease_(o.route.resources?o.route.resources->reserve(sizeof(Native)):ResourceLease{}),owner_(std::move(root),std::move(streams),std::move(o)){}
    MonitorControlDeclaration declared() const override{return owner_.declared();}
    MonitorOutputOrdinal first() const noexcept override{return owner_.declared().monitorFirst;}
    const std::filesystem::path &jobDirectory() const noexcept override{return owner_.jobDirectory();}
    void activate() override{owner_.activate();}
    MonitorControlCode requestStopAt(MonitorOutputOrdinal n) noexcept override{return owner_.requestStopAt(n);}
    MonitorControlCode receiveStopReceipt(MonitorControlReceipt &r) noexcept override{return owner_.receiveStopReceipt(r);}
    void requestCancel() noexcept override{owner_.requestCancel();}
    MonitorControlProgress progress() const noexcept override{const auto p=owner_.progress();return {p.processingDone,p.diskDone,p.captureEnded,p.renderEnded,p.rawAcceptedFrames,p.monitorAcceptedFrames,p.publishedMonitorFrames,p.deliveredMonitorFrames,p.copiedMonitorSourceFrames};}
    bool readyToJoin() override{return owner_.readyToJoin();}
    bool failedOwnerReadyToCancel() const noexcept override{return owner_.failedOwnerReadyToCancel();}
    MonitorControlOutcome wait() override{return owner_.wait();}
    MonitorControlOutcome cancelAndWait() override{return owner_.cancelAndWait();}
    EqEvent equalizerEvent(const EqSettings &s,const ParameterAddress &a,MonitorOutputOrdinal n) const override{return owner_.equalizerEvent(s,a,n);}
    EqEvent equalizerEnableEvent(bool e,MonitorOutputOrdinal n) const override{return owner_.equalizerEnableEvent(e,n);}
    SubmitStatus submitEqualizer(const EqEvent &e,std::uint64_t r) noexcept override{return owner_.submitEqualizer(e,r);}
    bool equalizerAcknowledgement(ImmediateAcknowledgement &a) noexcept override{return owner_.equalizerAcknowledgement(a);}
};
class PipeWire final:public PreparedMonitorControlEndpoint {
    ResourceLease lease_;PipeWireRecordingSpec declared_;MonitorOutputOrdinal first_;
    PipeWireMonitorRecording owner_;
  public:
    PipeWire(std::filesystem::path root,PipeWireMonitorStreamFactory factory,PipeWireMonitorRecordingOptions o,
        const std::vector<AudioPort> &i,const std::vector<AudioPort> &out)
        :lease_(o.route.resources?o.route.resources->reserve(sizeof(PipeWire)):ResourceLease{}),declared_(o.declared),first_(o.route.first),owner_(std::move(root),std::move(factory),std::move(o)){owner_.connect(i,out);}
    MonitorControlDeclaration declared() const override{return declared_;}
    MonitorOutputOrdinal first() const noexcept override{return first_;}
    const std::filesystem::path &jobDirectory() const noexcept override{return owner_.jobDirectory();}
    void activate() override{owner_.activate();}
    MonitorControlCode requestStopAt(MonitorOutputOrdinal n) noexcept override{return owner_.requestStopAt(n);}
    MonitorControlCode receiveStopReceipt(MonitorControlReceipt &r) noexcept override{return owner_.receiveStopReceipt(r);}
    void requestCancel() noexcept override{owner_.requestCancel();}
    MonitorControlProgress progress() const noexcept override{const auto p=owner_.progress();return {p.processingDone,p.diskDone,p.captureJoined,p.renderJoined,p.rawFrames,p.monitorInputFrames,p.publishedMonitorFrames,p.deliveredMonitorFrames,p.copiedSourceFrames};}
    bool readyToJoin() override{return owner_.service();}
    bool failedOwnerReadyToCancel() const noexcept override{return false;} // service handles native faults and joins.
    MonitorControlOutcome wait() override{return owner_.wait();}
    MonitorControlOutcome cancelAndWait() override{return owner_.cancelAndWait();}
    EqEvent equalizerEvent(const EqSettings &s,const ParameterAddress &a,MonitorOutputOrdinal n) const override{return owner_.equalizerEvent(s,a,n);}
    EqEvent equalizerEnableEvent(bool e,MonitorOutputOrdinal n) const override{return owner_.equalizerEnableEvent(e,n);}
    SubmitStatus submitEqualizer(const EqEvent &e,std::uint64_t r) noexcept override{return owner_.submitEqualizer(e,r);}
    bool equalizerAcknowledgement(ImmediateAcknowledgement &a) noexcept override{return owner_.equalizerAcknowledgement(a);}
};
}
std::unique_ptr<PreparedMonitorControlEndpoint> prepareNativeMonitorControlEndpoint(std::filesystem::path root,std::unique_ptr<NativeMonitorStreams> streams,NativeMonitorRecordingOptions o){return std::make_unique<Native>(std::move(root),std::move(streams),std::move(o));}
std::unique_ptr<PreparedMonitorControlEndpoint> preparePipeWireMonitorControlEndpoint(std::filesystem::path root,PipeWireMonitorStreamFactory factory,PipeWireMonitorRecordingOptions o,const std::vector<AudioPort> &i,const std::vector<AudioPort> &out){return std::make_unique<PipeWire>(std::move(root),std::move(factory),std::move(o),i,out);}
}
