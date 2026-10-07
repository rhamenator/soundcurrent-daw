// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <pipewire/filter.h>
#include <array>
struct HandoffFakePort {
    void *data = nullptr;
    unsigned frames = 0, calls = 0;
};
struct HandoffFake {
    const char *name = "sc-daw-test";
    struct Listener {
        const pw_filter_events *events = nullptr;
        void *context = nullptr;
        spa_hook *hook = nullptr;
    };
    std::array<Listener, 4> listeners{};
    std::array<HandoffFakePort, 65> ports{};
    unsigned listenerCount = 0, portCalls = 0;
    bool failPort = false;
    pw_direction direction{};
    pw_filter_port_flags flags{};
    std::size_t bytes = 0;
    pw_properties *props = nullptr;
    const spa_pod **params = nullptr;
    unsigned paramCount = 0;
    pw_filter *filter() {
        return reinterpret_cast<pw_filter *>(this);
    }
};
