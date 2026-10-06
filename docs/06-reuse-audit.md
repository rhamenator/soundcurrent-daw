# Existing equalizer work: read-only reuse audit

2026-10-05 owner clarification: code may be copied into the DAW and modified as needed, then useful improvements may return to the equalizer projects later. Preserve source revision, file hashes, notices and documented divergence. Existing repositories remain untouched in this implementation turn. See [ADR-007](decisions/007-windows-localization-and-reuse.md).

Inspection date: 2026-10-05. Workspace fingerprints before and after the task are in `research/workspace-before.json` and `research/workspace-after.json`. Applicable ancestry AGENTS.md files and repository AGENTS.md files were checked; none were present in these checkouts. User-provided Copperfin VM instructions remain applicable to future VM tests; this planning task uses no VM.

## Inspected repositories

| Repository | Identity | Observed processing boundary |
|---|---|---|
| Public free EQ | `/home/rich/dev/soundcurrent-eq`, main, `42af7c68a195c2bfc2a5de4f290005d85a3db567` | Linux Qt app manages PipeWire filter-chain DSP and routes; portable stereo DSP supports the Windows path. The Linux app is not a reusable in-process DAW graph. [Public repository](https://github.com/rhamenator/soundcurrent-eq), [pinned Linux implementation](https://github.com/rhamenator/soundcurrent-eq/blob/42af7c68a195c2bfc2a5de4f290005d85a3db567/src/main.cpp). |
| Windows EQ worktree | `/home/rich/dev/soundcurrent-eq-windows`, windows-port; exact HEAD in fingerprint | Existing platform branch preserved; inspect architecture without moving/switching this checkout. |
| Private premium EQ | `/home/rich/dev/soundcurrent-studio`, main, `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b` | Separate Qt-free C++20 DSP/engine/router/WAV SDK plus desktop bridge. This is reusable low-level processing code, not a timeline/recording/PDC/modular DAW engine. |

All were clean at inspection. Build outputs for feasibility go under `soundcurrent-daw/.cache`, including CMake's Studio subdirectory outputs. No commits, branches, files or audio settings in existing projects were changed.

## Reuse assessment

| Area | Existing evidence | Reuse decision / required changes |
|---|---|---|
| Controls | Qt frequency/band sliders, gain/balance, lock/undo, meters, tabs; Studio `src/studio_panel.*`, `src/studio_model.*`; public Qt monolith | Adapt control presentation/keyboard behaviors. Separate reusable widgets from global sink/settings logic; add stable IDs, unit descriptors, automation gestures and command transactions. Do not copy the entire main window as the DAW frontend. |
| Presets | Flat and musical profiles, band shapes/frequencies, speaker/amp profile metadata | Reuse generic EQ presets under existing GPL/notice terms with an explicit migration adapter. Preserve measurement provenance/rights independently. Receiver/speaker corrections belong on monitor paths by default. Existing profile numbers are not universally accurate room calibration. |
| Parameter model | Qt model plus band arrays/frequency/gain/Q; SDK `EngineSettings`/`ChannelSettings` vectors and scalar setters | Adopt units/ranges where appropriate; replace index-only identity with per-band/processor/instance IDs. Define smoothing, automation scope, sample timestamps and undo groups. Whole vector configure is not a safe concurrent RT parameter API. |
| DSP | Public `src/dsp.*` stereo biquads; private `src/engine.*`, `src/dsp.*`:1–256 channels,64-band cap, delay/reverb/router | Candidate EQ math/filters and response fixtures. Add DAW float-headroom policy (current engine clamps final samples), no automatic track attenuation, processor latency/tail/silence contract and lifecycle. New graph needed for buses, voices, PDC and scheduling. |
| Visualization | Public4096-point Hann-windowed radix2 FFT, colored per-band/overall peaks; GUI repaint timers | Reuse visual design/analysis concepts. Move transforms/multichannel aggregation to workers, use bounded decimation and prepared FFT library, keep phase/layout information. Never couple repaint interval to audio processing. |
| Analysis/calibration | Quiet logarithmic speaker/room sweep, background-noise/clipping rejection and preview; audio/profile parsing | Reuse offline analysis/measurement utilities after extraction and tests; microphone/speaker ambiguity remains documented. DAW monitoring corrections stay separate from prints. No physical sweep performed during this task. |
| Linux bridge | Private `src/linux_audio.cpp` uses `pw_stream`, FIFO,48 kHz defaults and `pw_thread_loop` control lock; callback serialized on loop, no RT_PROCESS flag | Port/device enumeration and error patterns are useful examples. Do **not** adopt this threading/routing/FIFO as the DAW scheduler. Build synchronous native multitrack ports with explicit timing and RT contracts. |
| Windows bridge | WASAPI capture/render and virtual cable routing in existing EQ platform sources | Later adapter patterns/format parsing may help; DAW needs device clock/latency/MIDI semantics and capture writer. VB-CABLE system-wide EQ route is not assumed necessary for ordinary DAW recording. No Windows parity claimed. |
| WAV and renderer | Private `src/wav.*`, `src/studio_render.cpp`; streamingPCM16/24/32,float32/classic/extensible; RIFF size bound | Good feasibility fixtures/readers. For pro recording select RF64/BW64/metadata/recovery-capable worker layer; do not inherit4 GiB cap or file writes on RT. Existing renderer is not a saved DAW session renderer. |
| Packaging | DEB, Fedora/RHEL RPM, desktop files/icons, Windows installer, license/notices and release checks | Reuse packaging conventions/scripts selectively with new identifiers, dependencies and versioning. Package installation cannot change default audio routing. New project remains unpublished. |
| Tests | DSP frequency response, engine channel/router/effects tests, WAV validation, Qt self-tests, SDK consumers/native bridge smoke | Reuse signal fixtures/testing style and host-independent SDK contract. New gates for graph swaps, PDC, event timing, recording recovery, session migration, media relink and plugin isolation are required. Passing EQ tests proves only EQ contracts. |

## SDK limits that affect design

- `AudioEngine::configure()` requires processing stopped and no concurrent configure/process. It can allocate filter/delay state. Build new instances on preparation thread, hand over at bounded block boundary; current scalar gain/mute setters apply on audio owner only.
- Planar and interleaved float processing are available; routing matrix is prepared separately. Finite outputs/clip reports are useful, but terminal clipping and preview headroom need a DAW-specific policy. Existing delay/reverb do not expose DAW-grade dynamic latency/tail declarations.
- Existing `maxChannels=256` and `maxStateBytes=128 MiB` are preview engine resource limits, not permanent DAW track/layout specifications. Large-channel simulation is not hardware/mixer/spatial qualification.
- The private SDK can build with `SOUNDCURRENT_BUILD_DESKTOP=OFF`; CMake exports `SoundCurrent::Engine`/`SoundCurrent::Wave`. This was confirmed externally without writing into its source tree.

## Reuse integration strategy

First slice consumes a pinned, audited module through an explicit local source option for experiments. Before shipping, either package an independently versioned GPL module with provenance or import the necessary source into this new repo with retained notices and upstream commit tracking. Do not depend on an absolute path to a private checkout in a distributable build. No automatic network fetch, no mutation of premium EQ and no assumption that a shared SDK gives all products identical functionality.

## Continuing source updates, 2026-10-06

The owner requires borrowed components to follow later equalizer development. See
[the additive source review and update procedure](25-equalizer-reuse-updates.md) and
[exact input inventory](../reuse/upstream-review.json). Both source trees currently
contain ongoing uncommitted work. This DAW reads them, freezes reviewed inputs and
adapts its own copies; no source-tree modifications or branch switches are made.
Run the read-only audit before subsequent reuse-dependent milestones.
