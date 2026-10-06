// SPDX-License-Identifier: GPL-3.0-only
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <soundcurrent/pipewire_filter.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace soundcurrent::daw {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw ProjectError(ErrorCode::Io, message);
}
std::uint64_t number(const spa_dict *props, const char *key) {
    const auto *p = spa_dict_lookup(props, key);
    if (!p)
        return 0;
    std::uint64_t n = 0;
    const auto last = p + std::char_traits<char>::length(p);
    const auto [end, error] = std::from_chars(p, last, n);
    return error == std::errc{} && end == last ? n : 0;
}
std::string text(const spa_dict *props, const char *key) {
    const auto *p = spa_dict_lookup(props, key);
    if (!p)
        return {};
    const auto n = std::char_traits<char>::length(p);
    if (n > 4096 || !validUtf8({p, n}))
        return {};
    return {p, n};
}
struct LoopLock {
    pw_thread_loop *loop;
    explicit LoopLock(pw_thread_loop *p) : loop(p) {
        pw_thread_loop_lock(loop);
    }
    ~LoopLock() {
        pw_thread_loop_unlock(loop);
    }
};
} // namespace
struct PipeWireFilter::State {
    PipeWireFilterOptions options;
    PipeWireCallbacks callbacks;
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    pw_filter *filter = nullptr;
    spa_hook filterListener{}, registryListener{}, coreListener{};
    std::array<void *, 256> inputs{}, outputs{};
    std::array<const float *, 256> inputViews{};
    std::array<float *, 256> outputViews{};
    std::unordered_map<std::uint32_t, PipeWirePort> nodes, remotePorts;
    std::unordered_set<std::uint32_t> routeLinks;
    struct Route {
        State &owner;
        pw_proxy *proxy = nullptr;
        spa_hook listener{};
        pw_link_state state = PW_LINK_STATE_INIT;
        bool listening = false;
        explicit Route(State &s) : owner(s) {}
        ~Route() {
            if (listening)
                spa_hook_remove(&listener);
            if (proxy)
                pw_proxy_destroy(proxy);
        }
        static void info(void *data, const pw_link_info *info) noexcept {
            auto &r = *static_cast<Route *>(data);
            if (info && (info->change_mask & PW_LINK_CHANGE_MASK_STATE)) {
                r.state = info->state;
                if (r.state == PW_LINK_STATE_ERROR)
                    r.owner.unavailable(AudioBridgeStatus::DeviceLost,
                                        "Selected audio route negotiation failed");
                else if (r.state != PW_LINK_STATE_ACTIVE &&
                         r.owner.admitted.load(std::memory_order_acquire))
                    r.owner.unavailable(AudioBridgeStatus::DeviceLost,
                                        "Selected audio route is no longer active");
            }
        }
        bool ready() const noexcept {
            return state == PW_LINK_STATE_ACTIVE;
        }
    };
    std::vector<std::unique_ptr<Route>> ownedLinks;
    std::string error;
    std::atomic<std::uint32_t> publishedNode{SPA_ID_INVALID}, shuttingDown{0};
    std::atomic<bool> admitted{false};
    bool active = false, started = false, inputsSet = false, outputsSet = false;
    bool initialized = false;

    void unavailable(AudioBridgeStatus status, const char *message) noexcept {
        if (shuttingDown.load(std::memory_order_acquire))
            return;
        admitted.store(false, std::memory_order_release);
        try {
            if (error.empty())
                error = message;
        } catch (...) {
        }
        if (callbacks.unavailable)
            callbacks.unavailable(callbacks.context, status);
    }
    static void stateChanged(void *data, pw_filter_state, pw_filter_state state,
                             const char *message) {
        auto &s = *static_cast<State *>(data);
        if (state == PW_FILTER_STATE_ERROR)
            s.unavailable(AudioBridgeStatus::DeviceLost,
                          message ? message : "PipeWire filter error");
        else if (state == PW_FILTER_STATE_PAUSED || state == PW_FILTER_STATE_STREAMING)
            s.publishedNode.store(pw_filter_get_node_id(s.filter), std::memory_order_release);
    }
    static void coreError(void *data, std::uint32_t, int, int, const char *message) {
        static_cast<State *>(data)->unavailable(AudioBridgeStatus::DeviceLost,
                                                message ? message : "PipeWire core error");
    }
    static void global(void *data, std::uint32_t id, std::uint32_t, const char *type, std::uint32_t,
                       const spa_dict *props) {
        auto &s = *static_cast<State *>(data);
        if (!props)
            return;
        try {
            if (std::string_view(type) == PW_TYPE_INTERFACE_Node) {
                PipeWirePort node;
                node.nodeId = id;
                node.nodeName = text(props, PW_KEY_NODE_NAME);
                node.nodeSerial = number(props, PW_KEY_OBJECT_SERIAL);
                node.mediaClass = text(props, PW_KEY_MEDIA_CLASS);
                require(s.nodes.contains(id) || s.nodes.size() < 16384,
                        "PipeWire node inventory limit exceeded");
                s.nodes[id] = std::move(node);
            } else if (std::string_view(type) == PW_TYPE_INTERFACE_Port) {
                if (text(props, PW_KEY_FORMAT_DSP) != "32 bit float mono audio")
                    return;
                const auto node = number(props, PW_KEY_NODE_ID);
                if (node > std::numeric_limits<std::uint32_t>::max())
                    return;
                PipeWirePort port;
                port.nodeId = static_cast<std::uint32_t>(node);
                port.portId = id;
                port.portName = text(props, PW_KEY_PORT_NAME);
                const auto direction = text(props, PW_KEY_PORT_DIRECTION);
                if (direction != "in" && direction != "out")
                    return;
                port.input = direction == "in";
                require(s.remotePorts.contains(id) || s.remotePorts.size() < 65536,
                        "PipeWire port inventory limit exceeded");
                s.remotePorts[id] = std::move(port);
            } else if (std::string_view(type) == PW_TYPE_INTERFACE_Link &&
                       (number(props, PW_KEY_LINK_INPUT_NODE) ==
                            s.publishedNode.load(std::memory_order_acquire) ||
                        number(props, PW_KEY_LINK_OUTPUT_NODE) ==
                            s.publishedNode.load(std::memory_order_acquire))) {
                require(s.routeLinks.contains(id) || s.routeLinks.size() < 65536,
                        "PipeWire link inventory limit exceeded");
                s.routeLinks.insert(id);
            }
        } catch (...) {
            s.unavailable(AudioBridgeStatus::DeviceLost, "PipeWire registry inventory failed");
        }
    }
    static void removed(void *data, std::uint32_t id) {
        auto &s = *static_cast<State *>(data);
        s.nodes.erase(id);
        s.remotePorts.erase(id);
        if (s.routeLinks.erase(id) && s.active)
            s.unavailable(AudioBridgeStatus::DeviceLost, "Selected audio route disconnected");
    }
    static void process(void *data, spa_io_position *position) noexcept {
        auto &s = *static_cast<State *>(data);
        if (s.callbacks.beginCallback)
            s.callbacks.beginCallback(s.callbacks.context);
        const auto end = [&] {
            if (s.callbacks.endCallback)
                s.callbacks.endCallback(s.callbacks.context);
        };
        if (!position) {
            end();
            return;
        }
        const auto &clock = position->clock;
        const DeviceBlockClock timing{clock.position,
                                      clock.duration,
                                      clock.nsec,
                                      clock.id,
                                      clock.cycle,
                                      clock.rate.num,
                                      clock.rate.denom,
                                      clock.delay,
                                      (clock.flags & SPA_IO_CLOCK_FLAG_XRUN_RECOVER) != 0,
                                      (clock.flags & SPA_IO_CLOCK_FLAG_DISCONT) != 0};
        if (s.callbacks.observeClock)
            s.callbacks.observeClock(s.callbacks.context, timing);
        if (s.shuttingDown.load(std::memory_order_acquire)) {
            end();
            return;
        }
        if (clock.duration == 0 || clock.duration > s.options.maximumNativeFrames) {
            // No capacity-certified views exist for this unsupported native
            // quantum. Signal outside the callback through the user's bridge.
            s.callbacks.process(s.callbacks.context, timing, {}, {}, 0);
            end();
            return;
        }
        const auto n = static_cast<std::uint32_t>(clock.duration);
        for (std::uint32_t c = 0; c < s.options.inputs; ++c)
            s.inputViews[c] = static_cast<const float *>(pw_filter_get_dsp_buffer(s.inputs[c], n));
        for (std::uint32_t c = 0; c < s.options.outputs; ++c)
            s.outputViews[c] = static_cast<float *>(pw_filter_get_dsp_buffer(s.outputs[c], n));
        if (!s.admitted.load(std::memory_order_acquire)) {
            // Native activation negotiates links. Until every owned link is
            // active, publish silence without capturing/advancing the engine.
            for (std::uint32_t c = 0; c < s.options.outputs; ++c)
                if (s.outputViews[c])
                    std::fill_n(s.outputViews[c], n, 0.f);
            end();
            return;
        }
        s.callbacks.process(s.callbacks.context, timing, {s.inputViews.data(), s.options.inputs},
                            {s.outputViews.data(), s.options.outputs}, n);
        end();
    }
    std::vector<PipeWirePort> ports() const {
        std::vector<PipeWirePort> result;
        result.reserve(remotePorts.size());
        for (const auto &[id, p] : remotePorts) {
            (void)id;
            const auto n = nodes.find(p.nodeId);
            if (n == nodes.end())
                continue;
            auto port = p;
            port.nodeName = n->second.nodeName;
            port.nodeSerial = n->second.nodeSerial;
            port.mediaClass = n->second.mediaClass;
            result.push_back(std::move(port));
        }
        return result;
    }
    void connect(const std::vector<PipeWirePort> &selected, bool input) {
        require(!active && filter, "Change PipeWire routes only while inactive");
        auto &configured = input ? inputsSet : outputsSet;
        require(!configured, "PipeWire routes already configured; prepare a new inactive adapter");
        const auto count = input ? options.inputs : options.outputs;
        require(selected.size() == count, "PipeWire route channel count mismatch");
        const auto all = ports();
        std::vector<std::array<std::string, 4>> descriptions;
        descriptions.reserve(count);
        // Preflight every channel before publishing any link. Numeric IDs alone
        // are insufficient: registry reuse must not select a different device.
        for (std::uint32_t c = 0; c < count; ++c) {
            const auto &p = selected[c];
            const auto valid = std::find_if(all.begin(), all.end(), [&](const auto &v) {
                return v.portId == p.portId && v.nodeId == p.nodeId &&
                       v.nodeSerial == p.nodeSerial && v.nodeName == p.nodeName &&
                       v.portName == p.portName && v.input == !input;
            });
            require(valid != all.end() && p.nodeId != publishedNode.load(std::memory_order_acquire),
                    "Selected PipeWire port is stale, wrong-direction or self-connected");
            const auto name = std::string(input ? "input_" : "output_") + std::to_string(c + 1);
            const auto own = std::find_if(all.begin(), all.end(), [&](const auto &v) {
                return v.nodeId == publishedNode.load(std::memory_order_acquire) &&
                       v.portName == name && v.input == input;
            });
            require(own != all.end(), "Own PipeWire port is not published");
            descriptions.push_back({std::to_string(input ? p.nodeId : own->nodeId),
                                    std::to_string(input ? p.portId : own->portId),
                                    std::to_string(input ? own->nodeId : p.nodeId),
                                    std::to_string(input ? own->portId : p.portId)});
        }
        ownedLinks.reserve(ownedLinks.size() + count);
        std::vector<std::unique_ptr<Route>> pending;
        pending.reserve(count);
        try {
            for (const auto &d : descriptions) {
                auto route = std::make_unique<Route>(*this);
                auto *props = pw_properties_new(
                    PW_KEY_LINK_OUTPUT_NODE, d[0].c_str(), PW_KEY_LINK_OUTPUT_PORT, d[1].c_str(),
                    PW_KEY_LINK_INPUT_NODE, d[2].c_str(), PW_KEY_LINK_INPUT_PORT, d[3].c_str(),
                    PW_KEY_OBJECT_LINGER, "false", nullptr);
                require(props, "Cannot allocate PipeWire link properties");
                route->proxy = static_cast<pw_proxy *>(
                    pw_core_create_object(core, "link-factory", PW_TYPE_INTERFACE_Link,
                                          PW_VERSION_LINK, &props->dict, 0));
                pw_properties_free(props);
                require(route->proxy, "Cannot create explicit PipeWire route");
                static const pw_link_events events{.version = PW_VERSION_LINK_EVENTS,
                                                   .info = Route::info};
                require(pw_link_add_listener(reinterpret_cast<pw_link *>(route->proxy),
                                             &route->listener, &events, route.get()) >= 0,
                        "Cannot observe explicit audio route negotiation");
                route->listening = true;
                pending.push_back(std::move(route));
            }
        } catch (...) {
            pending.clear();
            throw;
        }
        for (auto &route : pending)
            ownedLinks.push_back(std::move(route));
        configured = true;
        // Server-side rejection is asynchronous and delivered by coreError.
        // Do not claim that local publication proves remote link activation.
    }
};
PipeWireFilter::PipeWireFilter(PipeWireFilterOptions options, PipeWireCallbacks callbacks)
    : state_(std::make_unique<State>()) {
    require(!options.nodeName.empty() && options.nodeName.size() <= 256 &&
                validUtf8(options.nodeName) &&
                options.nodeName.find('\0') == options.nodeName.npos && callbacks.process &&
                options.inputs <= 256 && options.outputs <= 256 &&
                (options.inputs || options.outputs) && options.maximumNativeFrames >= 1 &&
                options.maximumNativeFrames <= 65536,
            "Invalid PipeWire filter configuration");
    auto &s = *state_;
    s.options = std::move(options);
    s.callbacks = callbacks;
    // Since PipeWire0.3.49, init/deinit are reference-counted and paired.
    // Balance this adapter's reference after all its context/data loops stop.
    pw_init(nullptr, nullptr);
    s.initialized = true;
    try {
        s.loop = pw_thread_loop_new(s.options.nodeName.c_str(), nullptr);
        require(s.loop, "Cannot create PipeWire control loop");
        s.context = pw_context_new(pw_thread_loop_get_loop(s.loop), nullptr, 0);
        require(s.context, "Cannot create PipeWire context");
        s.core = pw_context_connect(s.context, nullptr, 0);
        require(s.core, "Cannot connect to the user's PipeWire daemon");
        static const auto coreEvents = [] {
            pw_core_events e{};
            e.version = PW_VERSION_CORE_EVENTS;
            e.error = State::coreError;
            return e;
        }();
        pw_core_add_listener(s.core, &s.coreListener, &coreEvents, &s);
        s.registry = pw_core_get_registry(s.core, PW_VERSION_REGISTRY, 0);
        require(s.registry, "Cannot inventory PipeWire ports");
        static const pw_registry_events registryEvents{.version = PW_VERSION_REGISTRY_EVENTS,
                                                       .global = State::global,
                                                       .global_remove = State::removed};
        pw_registry_add_listener(s.registry, &s.registryListener, &registryEvents, &s);
        auto *props = pw_properties_new(
            PW_KEY_NODE_NAME, s.options.nodeName.c_str(), PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Duplex", PW_KEY_MEDIA_ROLE, "Production", PW_KEY_MEDIA_CLASS,
            "Audio/Filter", PW_KEY_NODE_AUTOCONNECT, "false", PW_KEY_NODE_DONT_RECONNECT, "true",
            PW_KEY_NODE_ALWAYS_PROCESS, s.options.alwaysProcess ? "true" : "false",
            PW_KEY_NODE_WANT_DRIVER, "true", nullptr);
        require(props, "Cannot allocate PipeWire filter properties");
        s.filter = pw_filter_new(s.core, s.options.nodeName.c_str(), props);
        require(s.filter, "Cannot create PipeWire DSP filter");
        static const auto filterEvents = [] {
            pw_filter_events e{};
            e.version = PW_VERSION_FILTER_EVENTS;
            e.state_changed = State::stateChanged;
            e.process = State::process;
            return e;
        }();
        pw_filter_add_listener(s.filter, &s.filterListener, &filterEvents, &s);
        for (bool input : {true, false}) {
            const auto count = input ? s.options.inputs : s.options.outputs;
            for (std::uint32_t c = 0; c < count; ++c) {
                const auto name = std::string(input ? "input_" : "output_") + std::to_string(c + 1);
                auto *p = pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio",
                                            PW_KEY_PORT_NAME, name.c_str(), nullptr);
                require(p, "Cannot allocate PipeWire DSP port properties");
                void *port =
                    pw_filter_add_port(s.filter, input ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT,
                                       PW_FILTER_PORT_FLAG_MAP_BUFFERS, 1, p, nullptr, 0);
                require(port, "Cannot create PipeWire DSP port");
                (input ? s.inputs : s.outputs)[c] = port;
            }
        }
        require(pw_filter_connect(s.filter,
                                  static_cast<pw_filter_flags>(PW_FILTER_FLAG_RT_PROCESS |
                                                               PW_FILTER_FLAG_INACTIVE),
                                  nullptr, 0) >= 0,
                "Cannot publish PipeWire DSP filter");
        require(pw_thread_loop_start(s.loop) >= 0, "Cannot start PipeWire control loop");
        s.started = true;
    } catch (...) {
        stop();
        throw;
    }
}
PipeWireFilter::~PipeWireFilter() {
    stop();
}
bool PipeWireFilter::waitReady(std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (!state_->loop)
            return false;
        {
            LoopLock lock(state_->loop);
            if (!state_->error.empty())
                return false;
            const auto id = state_->publishedNode.load(std::memory_order_acquire);
            const auto all = state_->ports();
            const auto n = std::count_if(all.begin(), all.end(),
                                         [&](const auto &p) { return p.nodeId == id; });
            if (id != SPA_ID_INVALID && n == state_->options.inputs + state_->options.outputs)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
std::vector<PipeWirePort> PipeWireFilter::ports() const {
    require(state_->loop, "PipeWire filter stopped");
    LoopLock lock(state_->loop);
    return state_->ports();
}
void PipeWireFilter::connectInputs(const std::vector<PipeWirePort> &p) {
    require(state_->loop, "PipeWire filter stopped");
    LoopLock lock(state_->loop);
    state_->connect(p, true);
}
void PipeWireFilter::connectOutputs(const std::vector<PipeWirePort> &p) {
    require(state_->loop, "PipeWire filter stopped");
    LoopLock lock(state_->loop);
    state_->connect(p, false);
}
void PipeWireFilter::activate() {
    require(state_->loop, "PipeWire filter stopped");
    {
        LoopLock lock(state_->loop);
        require(state_->error.empty() && !state_->active, "Cannot activate PipeWire filter");
        state_->admitted.store(state_->ownedLinks.empty(), std::memory_order_release);
        require(pw_filter_set_active(state_->filter, true) >= 0, "Cannot activate PipeWire filter");
        state_->active = true;
    }
    // All route negotiation and waits belong to the control owner. A proxy's
    // creation is not proof that every channel has negotiated its buffer.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until) {
        {
            LoopLock lock(state_->loop);
            require(state_->error.empty(), "Cannot activate PipeWire filter");
            if (std::all_of(state_->ownedLinks.begin(), state_->ownedLinks.end(),
                            [](const auto &r) { return r->ready(); })) {
                state_->admitted.store(true, std::memory_order_release);
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw ProjectError(ErrorCode::Io, "Selected audio routes did not finish negotiation");
}
void PipeWireFilter::stop() noexcept {
    if (!state_)
        return;
    auto &s = *state_;
    s.shuttingDown.store(1, std::memory_order_release);
    if (s.loop) {
        {
            LoopLock lock(s.loop);
            if (s.filter) {
                pw_filter_disconnect(s.filter);
                pw_filter_destroy(s.filter);
                s.filter = nullptr;
            }
            s.ownedLinks.clear();
            if (s.registry) {
                spa_hook_remove(&s.registryListener);
                pw_proxy_destroy(reinterpret_cast<pw_proxy *>(s.registry));
                s.registry = nullptr;
            }
            if (s.core) {
                spa_hook_remove(&s.coreListener);
                pw_core_disconnect(s.core);
                s.core = nullptr;
            }
            if (s.context) {
                pw_context_destroy(s.context);
                s.context = nullptr;
            }
        }
        if (s.started)
            pw_thread_loop_stop(s.loop);
        pw_thread_loop_destroy(s.loop);
        s.loop = nullptr;
        s.started = false;
    }
    if (s.initialized) {
        pw_deinit();
        s.initialized = false;
    }
}
std::uint32_t PipeWireFilter::nodeId() const noexcept {
    return state_->publishedNode.load(std::memory_order_acquire);
}
std::string PipeWireFilter::diagnostic() const {
    if (!state_->loop)
        return state_->error;
    LoopLock lock(state_->loop);
    return state_->error;
}
bool PipeWireFilter::memoryLocked() const noexcept {
    return false;
} // Qualification/reporting remains explicit.
} // namespace soundcurrent::daw
