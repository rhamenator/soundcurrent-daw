// SPDX-License-Identifier: GPL-3.0-only
// The production playback owner is compiled unchanged. Only the SDK-facing
// inventory/render symbols are replaced; no native endpoint is activated.
#include <soundcurrent/wasapi_playback.hpp>
#include <iostream>
#include <stdexcept>

using namespace soundcurrent::daw;
namespace {
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
struct Probe {
    ResourceLedger *ledger = nullptr;
    ResourceUsage baseline{};
    std::size_t outputBytes = 0;
    bool refuse = true;
    unsigned preparations = 0, activations = 0, defaultQueries = 0;
} probe;
struct Directory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        utf8Path("sc-output-owner-Δ-" + Id::generate().str());
    Directory() { std::filesystem::create_directory(root); }
    ~Directory() { std::error_code error; std::filesystem::remove_all(root, error); }
};
void unchangedCredit(const ResourceLedger &ledger, ResourceUsage before) {
    const auto after = ledger.usage();
    require(after.reservedBytes == before.reservedBytes && after.owners == before.owners,
            "Failed native preparation retained output memory credit");
}
void refusedActivation(WasapiPlayback &playback) {
    bool refused = false;
    try { playback.activate(); }
    catch (const ProjectError &e) { refused = e.code() == ErrorCode::InvalidState; }
    require(refused && !probe.activations, "Failed output route became active");
}
void rollback() {
    Directory directory;
    auto session = makeOneTrackSession("Output ownership", "Inactive source");
    session.tracks.front().layout = {LayoutKind::Stereo, 2};
    ProjectStore(directory.root).save(session);
    ResourceLedger ledger(64*1024*1024, "Output route test");
    probe = {}; probe.ledger = &ledger;
    const auto id = session.tracks.front().id;
    auto plan = identityMix(session, std::span(&id, 1), {LayoutKind::Stereo, 2});
    MixPlaybackConfig config; config.endFrame = 1024;
    config.graph.maximumFrames = 2048; config.graph.resources = ledger;
    ReadAheadOptions reader; reader.resources = ledger;
    probe.outputBytes = std::size_t(config.graph.maximumFrames)*2*sizeof(float) +
                        sizeof(PreparedWasapiOutput);
    {
        WasapiPlayback playback(directory.root, session, plan, config, reader);
        auto ports = playback.ports();
        require(ports.size() == 2, "Explicit mock render channels missing");
        probe.baseline = ledger.usage();
        require(probe.baseline.reservedBytes && probe.baseline.owners,
                "Production graph/reader escaped the shared ledger");
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            bool refused = false;
            try { playback.connectOutputs(ports); }
            catch (const ProjectError &e) { refused = e.code() == ErrorCode::Io; }
            require(refused, "Injected SDK preparation failure was hidden");
            unchangedCredit(ledger, probe.baseline);
            require(playback.status() == PlaybackBridgeStatus::Ready &&
                    !playback.position() && !playback.bufferFrames() && !playback.timing(),
                    "Failed inactive route published a stream or advanced processing");
            refusedActivation(playback);
        }
        probe.refuse = false;
        playback.connectOutputs(ports);
        auto admitted = ledger.usage();
        require(admitted.reservedBytes == probe.baseline.reservedBytes + probe.outputBytes &&
                admitted.owners == probe.baseline.owners + 1 && playback.bufferFrames() &&
                playback.timing() && !probe.activations && !playback.position(),
                "Retry did not commit exactly one prepared inactive output");
        // Replacing an existing inactive route must retire its owner too, then
        // leave no unused replacement if native preparation refuses.
        probe.refuse = true;
        bool refused = false;
        try { playback.connectOutputs(ports); }
        catch (const ProjectError &e) { refused = e.code() == ErrorCode::Io; }
        require(refused, "Inactive route replacement failure was hidden");
        unchangedCredit(ledger, probe.baseline);
        refusedActivation(playback);
        probe.refuse = false;
        playback.connectOutputs(ports);
        require(!probe.activations && !probe.defaultQueries && probe.preparations == 6,
                "Preparation activated or selected a default endpoint");
        playback.stop(); playback.stop(); playback.checkReader();
    }
    require(!ledger.usage().reservedBytes && !ledger.usage().owners,
            "Retired playback owner retained graph/reader/output credit");
    probe.ledger = nullptr;
}
} // namespace

// Link-time SDK test double, used exclusively by this executable. Native
// COM/event/driver preparation remains a separate Windows acceptance gate.
namespace soundcurrent::daw {
std::vector<WasapiEndpoint> wasapiEndpoints() {
    return {{"owned-test-render", "Explicit test renderer", false, 2, 48000}};
}
std::array<std::string, 6> wasapiDefaultEndpoints() {
    ++probe.defaultQueries; return {};
}
struct WasapiRenderStream::State { bool active = false; };
WasapiRenderStream::WasapiRenderStream(WasapiRenderOptions options,
                                     WasapiRenderCallbacks callbacks) {
    ++probe.preparations;
    require(options.endpointId == "owned-test-render" && options.channels == 2 &&
            options.sampleRate == 48000 && callbacks.fill && probe.ledger,
            "Mock SDK received a different route or missing context");
    const auto usage = probe.ledger->usage();
    require(usage.reservedBytes == probe.baseline.reservedBytes + probe.outputBytes &&
            usage.owners == probe.baseline.owners + 1,
            "Production output was not prepared before SDK admission");
    if (probe.refuse) throw ProjectError(ErrorCode::Io, "Injected SDK preparation refusal");
    state_ = std::make_unique<State>();
}
WasapiRenderStream::~WasapiRenderStream() = default;
void WasapiRenderStream::activate() { ++probe.activations; state_->active = true; }
void WasapiRenderStream::stop() noexcept { state_->active = false; }
bool WasapiRenderStream::drained() const noexcept { return false; }
std::uint64_t WasapiRenderStream::submittedFrames() const noexcept { return 0; }
std::uint32_t WasapiRenderStream::endGuardSubmittedFrames() const noexcept { return 0; }
std::uint64_t WasapiRenderStream::emptyQueueObservations() const noexcept { return 0; }
std::uint32_t WasapiRenderStream::bufferFrames() const noexcept { return 4800; }
NativeRenderTiming WasapiRenderStream::timing() const noexcept { return {}; }
std::optional<WasapiStreamFailure> WasapiRenderStream::failure() const noexcept { return {}; }
} // namespace soundcurrent::daw

int main() {
    try {
        rollback();
        std::cout << "Production playback owner: repeated admission rollback, inactive retry, "
                     "replacement and full resource retirement passed; SDK boundary injected\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
