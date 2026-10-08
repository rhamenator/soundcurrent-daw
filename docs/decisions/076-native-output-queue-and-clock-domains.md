# ADR076: native output queue and device clock are different domains

Status: accepted for the Windows playback preview boundary, 2026-10-08.

Use a prepared channel mapper over the shared in-process MixPlaybackRun and a
single WASAPI lease/COM owner. Keep project frame, queued frame sequence and raw
IAudioClock units separate. Preserve native frequency/QPC/padding observations;
do not label queued frames as physically played or fabricate a CaptureTimingOrigin.
Normal Finish drains before Complete; user Stop discards queued tail and joins
the native owner before disk reader/state retirement. Explicit native-rate/channel
admission precedes activation. Use system MMCSS scheduling registration/reversion
on each capture/render owner; fail preparation transparently if registration fails.

This avoids falsely satisfying alignment from contiguous software submissions.
It costs explicit device/timing metadata and a separate Windows UI preparation
adapter. Native-clock correlation, physical latency compensation, low-latency
duplex hardware qualification and sustained scheduling remain open. No new driver
or virtual cable dependency. See [implementation/evidence](../97-windows-native-playback.md).
