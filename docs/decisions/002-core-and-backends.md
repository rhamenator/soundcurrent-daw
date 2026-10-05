# ADR-002: Framework-independent core, system PipeWire/JACK

Status: **accepted boundary; implementation provisional**. Date: 2026-10-05.

Use C++20 and CMake. Keep session/processing core independent of Qt and OS routing. Native PipeWire is first adapter; JACK remains planned using existing libraries/services. Use identical processor graph semantics for live and offline execution.

Alternatives: wholesale EQ bridge reuse lacks DAW timing/recording/PDC; JUCE/Tracktion can reduce implementation work but introduce framework/license coupling; full workstation forks impose another session/UI architecture. Keep serious reuse alternatives in M0 evaluation before expanding the scheduler or host.

Evidence: Studio's headless SDK compiled and passed the bounded EQ/WAV probe. This supports low-level DSP reuse, not the whole architecture. Follow the [threading contract](../02-architecture.md).
