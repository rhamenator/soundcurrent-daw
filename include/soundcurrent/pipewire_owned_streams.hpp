// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pipewire_monitor_recording.hpp"
#include "pipewire_monitor_capture_stream.hpp"
#include "pipewire_monitor_render_stream.hpp"
namespace soundcurrent::daw {
struct PipeWireOwnedStreamOptions {
    PipeWireMonitorCaptureStreamOptions capture;
    PipeWireMonitorRenderStreamOptions render;
};
// Genuine inactive Linux filters. Actual graph callbacks verify expected
// geometry. Select routes explicitly; no device/default/rate/quantum changes.
std::unique_ptr<PipeWireMonitorStreams> preparePipeWireOwnedStreams(PreparedPipeWireMonitorRoute &,PipeWireOwnedStreamOptions = {});
} // namespace soundcurrent::daw
