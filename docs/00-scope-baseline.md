# Product scope and frozen reference baseline

Decision date: **2026-10-05**. Baseline ID: **SC-DAW-BASELINE-2026-10-05**.

## Product commitment

Build a Linux-first professional workstation that supports the combined workflows of **full Bitwig Studio** and **Cubase Pro**, including composition, recording, editing, sound design, live performance, mixing, immersive production, scoring, restoration, and reliable interchange. Milestones sequence the work; late milestones remain requirements. A recorder or a matching list of feature labels is not the finished product.

C++20 and a CMake build are accepted defaults. Qt 6 is provisional for the GUI; the processing/session core must compile without Qt. Native PipeWire is the first Linux backend, with JACK support through existing infrastructure. The owner added **Windows functional parity** and **localization across all of Europe** on 2026-10-05; these are required staged deliverables, with portability starting in the core and native adapters/UI tests later. [Platform and language gates](08-platforms-and-localization.md) supplement the frozen vendor references without changing their versions. There is no commitment to commercial products' exact UI, proprietary DSP implementations, branded content, or undisclosed project file formats.

This repository is `soundcurrent-daw`: `/home/rich/dev/soundcurrent-studio` already serves the premium equalizer. Initial planning inspected those repositories without changing their work. The owner subsequently activated the full implementation goal and authorized the public GPL source backup at https://github.com/rhamenator/soundcurrent-daw. Planning is no longer the stopping boundary; see [active goal](../GOAL.md) and [progress](12-goal-progress.md).

The owner added **X006 studio track scalability** on2026-10-06: serve lower-budget studios, including studios with substantial hardware, without a fixed product/license ceiling on total project tracks. Real-time capacity and simultaneous hardware inputs remain separately admitted/measured. [Track scalability](67-track-scalability.md) defines staged acceptance and the present256-track implementation gap. This owner extension does not change the frozen vendor reference versions or claim unlimited processing throughput.

## Reference freeze

The owner added **X007 easy installation** and **useful workflow previews** on
2026-10-07. Deliver normal Linux/Windows setup without development tools and qualify
upgrades/removal that preserve recordings. Begin with a tested Linux recording/EQ/
project/WAV preview, retaining explicit platform and capability gaps. See
[installation](88-easy-installation.md) and [preview delivery](89-workflow-previews.md).
These priorities do not change the frozen versions or reduce full-suite parity.

| Reference | Frozen executable baseline | Documentation baseline | Evidence and limitation |
|---|---|---|---|
| Bitwig Studio, full edition | **6.1.3**, released **2026-09-25** | General guide **5.3**, official 6.1 PDF incorporating 6.0 changes, 6.1.3 changelog | [Download page](https://www.bitwig.com/download/), [versioned release notes](https://www.bitwig.com/dl/Bitwig%20Studio/6.1.3/release_notes/), [6.1/6.0 documentation](https://downloads.bitwig.com/6.1/Release-Notes-6.1.pdf), [general guide](https://www.bitwig.com/userguide/latest/). Official release notes acknowledge the general manual overhaul. Old-guide evidence is provisional for unchanged 6.x workflows. |
| Cubase Pro | **15.0.30**, updated **2026-06-03** | Pro 15.0 webhelp; operation PDF identifies **15.0.30**, dated **2026-06-03**; separate Pro 15 score guide | [Official download list](https://o.steinberg.net/en/support/downloads/cubase_15.html), [Pro operation manual](https://www.steinberg.help/r/cubase-pro/15.0/en), [operation PDF](https://www.steinberg.help/api/khub/documents/O4PvzgK5U8lOyn4ANi_gKg/content), [score guide](https://www.steinberg.help/r/cubase-pro/cubasescore/15.0/en). API document IDs and webhelp paths are mutable; retrieved-byte hashes capture the inspection snapshot. |

These are the latest versions shown by the inspected official download pages on the freeze date. Do not advance the reference silently. A later baseline change requires an ADR, refreshed matrix evidence, and new regression fixtures. No reference executable was installed or evaluated in this planning task. A licensed reference application evaluation remains a milestone gate; documentary evidence alone cannot establish behavioral or sonic parity.

The shorter cached Cubase search result suggested 15.0.20; the directly retrieved PDF and download page both establish 15.0.30. Bitwig search caches similarly lagged the live download page. Use the directly inspected versions above.

## Four independent measures

| Axis | Meaning | Required evidence |
|---|---|---|
| Functional parity (F) | The required task can be completed, with equivalent important options, automation behavior, failure handling and interchange consequences | Recorded end-to-end acceptance workflows against the frozen references, including edge cases; no name-only passes |
| Processing quality (Q) | Timing, artifacts, noise, accuracy, headroom, CPU and latency meet the published acceptance envelope | Repeatable signal tests, blind level-matched listening, reference renders where legally available, realistic load measurements |
| Bundled content (C) | Original or licensed instruments, presets and material cover the musical use cases and breadth | Category/coverage inventory, rights manifest, audition/usefulness review; vendor libraries are not copied |
| Native-project compatibility (N) | Explicit import/export fidelity for `.bwproject`, `.cpr`, and other proprietary states | Documented format access plus round-trip corpus tests and a loss report; **currently unknown and unimplemented**, never inferred from DAWproject/plugin support |

N is a separately tracked research commitment. Native compatibility is not a precondition for implementing equivalent workflows, nor is it claimed. Transparent interoperability loss reports are mandatory. If a format cannot be implemented using permitted specifications/access, report that gap and request an explicit product decision rather than relabeling an exchange format as native compatibility.

## Scope that must stay visible

- Recording/editing: audio and MIDI, takes/comping, multi-microphone phase coherence, monitoring, punch/loop, tempo-aware warping and segmented pitch/formant editing.
- Mixing: buses/sends/sidechains/VCA, measured external round trips, multichannel layouts, latency compensation and tails, nonprinting monitor correction/cues/talkback.
- Composition/performance: expressive notes, automation modes, tempo/signature maps, articulation maps and attack offsets, scoring, controller mapping, launcher/scenes/performance capture, hybrid tracks, nested devices, polyphonic modulation and modular sound design.
- Hosting: native Linux VST3/CLAP/LV2, reliable discovery/state/editor integration, crash containment, automation and layouts. Windows-only plugin bridges are a separate evaluated compatibility task. Required legacy VST2 workflows stay in the gap register pending lawful SDK access.
- Media/content/delivery: sampling and synthesis, effects and browser/preset ecosystem, surround/Ambisonics/object workflows, video/timecode, stem separation, analysis, formats/interchange, rendering/freeze/batch export.
- Durability: portable assets, stable state, undo/redo, plugin/media placeholders, autosave, interrupted recordings, migration, backup/restore and failure diagnostics.

Commercial names are reference landmarks, not licensed assets. Dolby-branded delivery, proprietary algorithms, vendor control protocols, native files, AI models, vocal synthesis and bundled companion software need individual feasibility/rights decisions. They remain visible in the roadmap and gap register.

## Source discipline

`research/sources.json` records canonical URLs, retrieval date and SHA-256. Raw documents are in ignored `.cache/sources/`; the tracked plan includes original workflow specifications and brief factual evidence, not copied manuals. `/latest/` and `/15.0/` URLs are paired with this date and hashes because they are not immutable. An absent manual or marketing entry means **U (unknown)**, never a negative feature claim. The matrix is a first requirements decomposition, not proof that every button and bundled preset has already been catalogued; M0 must expand the exhaustive subfeature inventory before parity can be certified.

X006 scoped controller-memory checkpoint: [shared parent ownership and desktop
policies](83-controller-memory-resources.md) now cover canonical/history/snapshot
and declared edit work. [GUI payload admission](84-gui-memory-resources.md) adds
selected-track copies, list/decorations and timeline/query arrays with staged
refusal/retry. [Prepared execution admission](85-graph-memory-resources.md) now
adds DSP/playback, readers, shared media caches and WAV output buffers. Capture/other
IO, exact allocations/RSS and sustained capacity remain unqualified. No reference or F/Q/C/N completion change.
