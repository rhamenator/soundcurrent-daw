// SPDX-License-Identifier: GPL-3.0-only
// Standalone system-PipeWire lifecycle probe; no DAW, ports or audio routes.
// Normal: three contexts. --init-only: initialization baseline.
#include <pipewire/pipewire.h>
#include <cstdio>
#include <string_view>
int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--init-only"))
        return 2;
    pw_init(nullptr, nullptr);
    if (argc == 1) {
        for (int i = 0; i < 3; ++i) {
            auto *loop = pw_main_loop_new(nullptr);
            if (!loop) {
                pw_deinit();
                return 3;
            }
            auto *context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
            if (!context) {
                pw_main_loop_destroy(loop);
                pw_deinit();
                return 4;
            }
            pw_context_destroy(context);
            pw_main_loop_destroy(loop);
        }
    }
    pw_deinit();
    std::puts("PipeWire lifecycle probe complete");
}
