### Goal prompt

Bring **SoundCurrent DAW Studio** to a complete, tested, installable professional digital audio workstation and recording suite.

Work in `/home/rich/dev/soundcurrent-daw`. Begin by reading applicable `AGENTS.md` instructions, the repository’s planning artifacts, decision records, implementation, and test evidence. Continue from existing work rather than restarting it.

#### Product target

Deliver the combined functional capabilities of these frozen references:

- **Full Bitwig Studio 6.1.3**
- **Cubase Pro 15.0.30**
- Baseline: **SC-DAW-BASELINE-2026-10-05**

Preserve this baseline until I explicitly change it. Use the existing source-linked parity matrix and expand feature families into detailed, testable workflows. Do not silently exclude difficult features, substitute a basic recorder for the intended product, or claim parity from feature names alone.

Develop Linux first and deliver a **Windows version with the same functional capabilities**. Use **C++20, CMake, and Qt 6** unless evidence justifies a documented change. The suite is **GPL-3.0-only**; paid distribution remains possible while preserving GPL rights and corresponding-source obligations.

#### Required capabilities

Complete the staged implementation of:

- Multitrack audio/MIDI recording, monitoring, punch/loop recording, take lanes, comping, grouped edits, fades, stretching, and pitch editing.
- Mixing, buses, sends, sidechains, VCA controls, external hardware routing, recording alignment, and plugin delay compensation.
- Automation modes, expressive note control, tempo/meter maps, articulation/expression maps, notation, and controller integration.
- Clip launching, scenes, launch quantization, performance capture, hybrid tracks, device containers, modulation, and modular sound design.
- VST3/CLAP hosting and Linux LV2 support, including discovery, state restoration, editor integration, and crash isolation.
- Control-room monitoring, cue mixes, talkback, monitoring-only room correction, metering, and loudness analysis.
- Instruments, effects, sampling, content browsing, surround/Ambisonics, video/timecode, stem separation, export, and interchange.
- Portable projects, missing-media/plugin handling, comprehensive undo/redo, autosave, interrupted-recording recovery, migrations, and backups.

Track **functional parity, processing-quality parity, bundled-content coverage, and native-project compatibility separately**. Implement original or properly licensed content and algorithms. Never assume proprietary assets, algorithms, or project formats can be copied. Keep unresolved compatibility and licensing gaps visible.

#### Localization

Make localization part of both Linux and Windows development.

Cover **all of Europe**, including non-EU countries and regional/minority languages. Expand and audit the existing language register; its initial 130 work items are not exhaustive coverage or completed translations.

Use stable language-independent identifiers and project formats, Unicode-safe names and paths, contextual translations, correct plurals and locale formatting, appropriate script/font support, and bidirectional layouts where needed. Track translation completeness, native-speaker review, and UI qualification separately for each language. Include application controls, errors, recovery, help, and installer text. Empty catalogs must never count as supported languages.

#### Engineering requirements

Maintain a framework-independent engine shared by live playback and offline rendering.

Use existing PipeWire/JACK infrastructure on Linux and appropriate native Windows audio adapters. Do not recreate or fork PipeWire. Keep GUI work, disk I/O, allocations, blocking locks, and logging outside real-time callbacks.

Implement and verify bounded queues, safe graph replacement and object retirement, timing domains, channel layouts, stable parameter IDs, sample-accurate events, processor latency/tails, and versioned project state. Preserve floating-point headroom inside the graph.

Evaluate dependencies for functionality, licensing, maintenance, platform support, and integration cost before adopting them. Maintain provenance, dependency notices, and reproducible builds.

Borrow useful code from the equalizer projects when justified. Copy and adapt it within the DAW repository, retaining original notices, exact source revisions, and documented changes. Record improvements suitable for later upstream adoption. Do not modify the equalizer repositories, switch their branches, or disturb their working trees unless I explicitly authorize that work.

#### Execution

Proceed autonomously through the existing milestones. Resolve routine implementation choices without repeated permission requests; ask only about consequential unresolved product decisions.

Start with **S3: the prepared in-process EQ**, including auditable provenance, float headroom, bounded parameter events, and smoothing. Then complete capture, native audio integration, UI, and offline export to finish the first recording slice. Continue through the full staged backlog afterward.

For each milestone:

1. Implement concrete, usable workflows.
2. Add meaningful acceptance and failure tests.
3. Validate processing quality, real-time behavior, persistence, and recovery.
4. Verify platform-specific behavior independently.
5. Record evidence, remaining gaps, and the next implementation task.
6. Make coherent local commits and keep documentation current.

Use synthetic sources and owned test routes before physical audio tests. Preserve ongoing user playback where possible; coordinate audible tests that require interruption.

Before local Copperfin VM testing, read `/home/rich/Documents/Codex/2026-10-03/se/CLAUDE.md` and follow its policies. Preserve original VMs and the pristine template; use full independent clones for risky or system-changing tests. Never put credentials in source files or logs.

Do not publish, push, or upload releases without explicit authorization. Prepare reviewable release artifacts locally.

2026-10-06 authorization: publish `soundcurrent-daw` as a public GPL-3.0 GitHub
repository for source/history backup and prepare a copy for the owner's Windows
machine. This authorizes repository creation and source pushes; it does not claim
a qualified release or installer. See docs/57-repository-backup.md.

#### Completion criteria

Do not declare completion until:

- Required frozen-reference workflows have detailed acceptance evidence.
- Linux and Windows builds, installers, and native workflows are qualified.
- Processing-quality gates, real-time constraints, recording recovery, project portability, and security requirements pass.
- European language coverage is audited and each delivered language has recorded translation/review/UI qualification.
- Content and dependency licensing, notices, and GPL source delivery are complete.
- Remaining gaps are explicitly resolved or accepted by me.

Report external blockers honestly and continue independent work where possible. A partial milestone, successful compilation, or matching feature list does not establish completion.

#### Additional owner requirement (2026-10-05)

X004: import other suites' work/project files. Implement native-project and exchange-format adapters with versioned preservation/loss evidence; see docs/10-project-import.md. This extends the active goal and does not shrink the frozen parity target.

#### Additional owner requirement: equipment profiles (2026-10-05)

X005: bring the equalizer projects' expanded equipment profiling and profile editor into this DAW. Cover microphone, speaker, amplifier/receiver and qualified whole-system measurements; import, brand/family/model/variant browsing, response/correction curves, editable custom copies, provenance and save prompts for modified profiles. Reuse reviewed GPL snapshots with exact source/data rights and hashes, without altering the ongoing equalizer work. Keep monitoring calibration separate from raw takes/default exports; explicit correction printing, portable profile state, Linux/Windows parity and all-Europe localization need acceptance evidence. See docs/17-equipment-profiles.md. This extends the active goal without shrinking its frozen-reference requirements.

### Continuing equalizer reuse updates (2026-10-06)

Keep borrowed components aligned with reviewed changes from the two equalizer projects.
Run the read-only input audit before reuse-dependent milestones; review changed inputs,
retain exact committed or working-snapshot hashes/notices, adapt DAW copies and qualify
affected workflows. Preserve deliberate DAW-specific behavior and existing project
compatibility. Do not modify the equalizer source trees or silently merge unreviewed
code/data. See docs/25-equalizer-reuse-updates.md.

### Additional owner requirement: studio track scalability (2026-10-06)

X006: target lower-budget recording studios, including studios with substantial
audio hardware. Support any finite project track count that fits admitted machine
resources, without a fixed product/license ceiling. Separate total tracks, active
processing and simultaneous hardware inputs; scale model/parser/graph/recording,
media caches, UI/history and persistence together. Preserve bounded real-time
execution, safe refusal, originals and recovery. Include freeze/unfreeze, bounce,
offline rendering and measured Linux/Windows workload profiles. The present256
track limit remains a documented implementation gap until qualified. See
docs/67-track-scalability.md. This extends the goal without changing the frozen
reference versions, reducing other requirements or claiming unlimited real-time
performance.
