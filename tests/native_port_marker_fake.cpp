// SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>
// Separate translation unit: external test calls resolve through --wrap; the
// wrapper's __real reference resolves to this unchanged fake buffer provider.
extern "C" void *pw_filter_get_dsp_buffer(void *p, std::uint32_t) {
    return p;
}
