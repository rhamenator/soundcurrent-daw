// SPDX-License-Identifier: GPL-3.0-only
#include "native_port_handoff_fake.hpp"
extern "C" const char *pw_filter_get_name(pw_filter *filter) {
    return reinterpret_cast<HandoffFake *>(filter)->name;
}
extern "C" void pw_filter_add_listener(pw_filter *filter, spa_hook *hook,
                                       const pw_filter_events *events, void *context) {
    auto &f = *reinterpret_cast<HandoffFake *>(filter);
    f.listeners.at(f.listenerCount++) = {events, context, hook};
}
extern "C" void *pw_filter_add_port(pw_filter *filter, pw_direction direction,
                                    pw_filter_port_flags flags, std::size_t bytes,
                                    pw_properties *props, const spa_pod **params, unsigned count) {
    auto &f = *reinterpret_cast<HandoffFake *>(filter);
    f.direction = direction;
    f.flags = flags;
    f.bytes = bytes;
    f.props = props;
    f.params = params;
    f.paramCount = count;
    auto *p = &f.ports.at(f.portCalls++);
    return f.failPort ? nullptr : p;
}
extern "C" void *pw_filter_get_dsp_buffer(void *key, unsigned frames) {
    if (!key)
        return nullptr;
    auto &p = *static_cast<HandoffFakePort *>(key);
    ++p.calls;
    p.frames = frames;
    return p.data;
}
