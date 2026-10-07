// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/resource_ledger.hpp>
#include <unordered_map>
namespace soundcurrent::daw::ui {
// Declared GUI ownership, not an exact allocator/RSS measurement. All callers
// are on control/GUI threads; no accounting or last-owner release is RT-safe.
inline void mapAllowance(PayloadCharge &bytes, std::size_t count, std::size_t keys,
                         std::size_t value) {
    bytes.add(count, value + 256); // nodes/linkage and conservative validation allowance
    bytes.add(keys);
    bytes.add(count, sizeof(void *) * 4);
    bytes.add(32, sizeof(void *));
}
template <class V>
void mapCharge(PayloadCharge &bytes, const std::unordered_map<std::string, V> &map) {
    bytes.add(map.size(), sizeof(typename std::unordered_map<std::string, V>::value_type) + 256);
    bytes.add(map.bucket_count(), sizeof(void *));
    for (const auto &[key, value] : map)
        bytes.add(key.capacity() + 1);
}
inline PayloadCharge guiCharge(const char *label) {
    PayloadCharge bytes(label, std::numeric_limits<std::size_t>::max());
    bytes.add(256);
    return bytes;
}
} // namespace soundcurrent::daw::ui
