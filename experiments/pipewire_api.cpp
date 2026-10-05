// SPDX-License-Identifier: GPL-3.0-only
// Header/link/atomics check only. Does not connect or create audio nodes.
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <atomic>
#include <cstdint>
#include <iostream>
int main() {
    std::atomic<std::uint64_t> index{0};
    std::cout << "{\"pipewire_library\":\"" << pw_get_library_version()
              << "\",\"filter_rt_process_flag\":" << PW_FILTER_FLAG_RT_PROCESS
              << ",\"filter_events_bytes\":" << sizeof(pw_filter_events)
              << ",\"uint64_atomic_lock_free\":" << (index.is_lock_free()?"true":"false")
              << ",\"connected\":false}\n";
    return index.is_lock_free()?0:1;
}
