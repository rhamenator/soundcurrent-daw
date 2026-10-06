# SLICE-001: record one track → in-process EQ → save/reopen → WAV

Status: **S1/S2 state, S3 prepared EQ/transport, S4 headless recording/recovery, S5 native owned-source and S6a bounded playback and S6b immediate-control and S6c asynchronous project-editor and S6d/S6e native owner/desktop playback and S6f recording owner/attachment and S6g desktop recording/recovery and S7a/S7b offline/desktop export foundations implemented; whole slice incomplete** (2026-10-06). State includes stable IDs, scalar EQ/route/monitor-preference undo, v1.2 snapshots with explicit v1.0/v1.1 migration and hashed relative media. `sc-engine` supplies prepared peaking EQ, sample-timed events, smoothing, float headroom and shared live/offline processing; bounded queue and retirement infrastructure have fixtures. See [state](09-session-state-contract.md), [engine](11-engine-contract.md) and [results](../tests/results/SLICE-001/). Linux tests/sanitizers run; Windows core/tests cross-compile. A concurrent synthetic source records RF64 raw takes, checkpoints/recovery and project attachment; see [recording contract](13-recording-contract.md). The shared bridge and native PipeWire owned-source/monitor/disconnect fixtures now exist; full graph transitions, hardware latency, automatic reprepare, Windows audio, general graph UI and full platform/route qualification remain unimplemented or unqualified. See [native contract](14-native-audio-contract.md). Physical disk-full/power-loss, >4 GiB and native Windows execution remain unverified.

## User workflow

1. Launch from the desktop/app menu. Create a project with48 kHz rate and one mono audio track. Select an available PipeWire input and explicit monitor output; do not change system-wide defaults.
2. Arm the track, select monitoring off/auto/on, and record at least ten seconds. Show elapsed recorded frames, input peak and any gap/error. The captured source remains raw so later EQ edits do not destroy it.
3. Stop; play the take through an **in-process** three-band parametric EQ (frequency/gain/Q plus enable, with persistent band IDs). Move gain/frequency while playing, hear bounded immediate response, and undo a control gesture.
4. Save the project. Close and reopen it. Restore media, track/routing intent, EQ parameters, sample position and rates. Missing devices show a clear placeholder rather than an arbitrary default output.
5. Export the selected range as48 kHz mono float32 WAV through the same EQ graph, with optional PCM24 as a follow-up within the slice only if format fixtures pass. Reopen exported WAV and compare it with deterministic live-engine capture. Correction/dim/talkback remain outside the export path as architecture defaults even before those controls exist.

Acceptance first uses a **synthetic PipeWire test source** owned by the test, without physical output links. Physical microphone/speaker testing is scheduled separately when music/background noise permit it. Current authorization from earlier EQ testing is not a reason to interrupt today's playback during unrelated work.

## Implement in dependency order

| Task | Deliverable | Verification |
|---|---|---|
| S1: Core scaffolding | Qt-free `sc-session`, `sc-engine`, stable UUID/parameter descriptors, mono layout, frame clock; CMake target boundaries | Headless consumer builds without Qt/device headers; IDs remain stable through rename/order and serialization |
| S2: Session state | Version1 JSON schema, media manifest with hashes and relative paths, one semantic undo gesture, validated atomic save/load | Save temp/rename failure keeps previous valid snapshot; move project directory and reopen; unknown version rejected clearly |
| S3: DSP wrapper | Prepared three-band EQ using audited GPL coefficients, float headroom, sample-offset control events and smoothing; same code offline | Flat/unity/response and block-partition fixtures; +6 dB overs preserved inside graph; allocation instrument; unchanged filter histories across scalar gestures |
| S4: Capture writer | Fixed slab pool/SPSC with sequence/frame extents; disk-worker libsndfile WAV/RF64, journal and finalize | Synthetic known waveform and ramp timestamps; disk-full/queue-full reports; valid prefix recoverable; no file APIs in processing |
| S5: PipeWire adapter | Normal-user native filter ports, negotiated planarfloat/quantum/latency; selected input/output lifecycle | Owned virtual source capture; no hardware/default-route changes; rate/quantum change triggers prepared restart; disconnect shown |
| S6a: Playback read-ahead | Bounded decoded slab pool, timeline/source mapping, seek generations, playback through prepared EQ | Exact source extents; underflow silence/counters; stale seek data retired off RT; no file APIs or allocations in callback |
| S6b: Immediate control ingress | Independent bounded edits, sample-frame receipts and recording/playback forwarding | Mixed automation/gesture ordering; exact offline replay; concurrent prefix bound and RT audit |
| S6c: Project controller/editor | Separate model/I/O workers, bounded GUI commands, immutable snapshots, scalar editor and dirty-close barrier | Save while editing, undo to saved content, cancellation/pressure/close choices, focus/keyboard/scroll fixtures |
| S6e: Desktop playback | Asynchronous output selection, playback/stop, model-to-DSP revision reconciliation and colored output peaks | Owned native GUI samples with exact receipts/offline replay, queue/close/failure fixtures; Windows GUI/native still open |
| S6f: Recording owner/attachment | Inactive explicit input/monitor routes, activation-time jobs, raw-before-DSP capture, native/writer joins and asynchronous verified take admission | Native off/stop/cancel/write-failure/destructor/activation failures; alias/fault races and unchanged canonical model on rejected receipts |
| S6g: Desktop recording/recovery | Separate recording worker, explicit inputs/arm/Record/Off/Post-EQ monitoring, retained verified take handoff, close barrier and manual recovery copy | Controller pressure/fault/handoff, delayed modal close, synthetic-disk GUI and owned native signal/receipt/save/reopen fixtures; platform/routing/discovery gates remain |
| S6: Qt UI | One track transport/arm/monitor/EQ controls, level meter, Save/Open/Export, keyboard labels and desktop integration | Fits1280x720; wheel changes require intended control focus; undo gesture exact; all main actions keyboard accessible |
| S7: Offline export | S7a core/CLI: private shared EQ, exact range/preroll/tail, float WAV/RF64 and cancellable transaction; S7b desktop capture/options/consent/progress/cancel/join binding now exists | Frame count/rate/channel data exact; no overwrite without user confirmation; cancellation leaves no published partial file |
| S8: End-to-end acceptance | Recorded evidence of create/record/EQ/save/reopen/export | All gates below pass, actual device test separately documented from synthetic results |

No clip launcher, VST host, notation, new effects catalog, arbitrary-channel UI or native project decoder belongs in SLICE-001. Those requirements retain their milestones.

## Proposed initial state shape

```json
{
  "format": "soundcurrent-daw",
  "schemaMajor": 1,
  "schemaMinor": 1,
  "projectId": "UUID",
  "sampleRate": 48000,
  "tracks": [{
    "id": "UUID",
    "layout": {"kind": "mono", "channels": 1},
    "inputIntent": {"backendId": "pipewire", "portIdentity": "",
                    "ports": [{"deviceIdentity": "microphone-node",
                               "portIdentity": "capture_1", "mediaClass": "Audio/Source",
                               "input": false}]},
    "outputIntent": {"backendId": "pipewire", "portIdentity": "", "ports": [null]},
    "monitorIntent": {"backendId": "", "portIdentity": "", "ports": []},
    "clips": [{"id": "UUID", "assetId": "UUID", "startFrame": 0,
               "sourceFrame": 0, "lengthFrames": 480000}],
    "processors": [{"id": "UUID", "type": "sc.eq", "version": 1,
                    "bands": [{"id": "UUID", "frequencyHz": 1000,
                               "gainDb": 0, "q": 1}]}]
  }],
  "assets": [{"id": "UUID", "path": "media/take-UUID.wav", "sha256": "hex",
              "sampleRate": 48000, "channels": 1, "frames": 480000}]
}
```

This partial example illustrates identity, timing and typed routing. The implemented v1.1 encoding additionally includes session/track names, playhead/export range, processor enable and structured asset layout; exact fields are defined by the encoder and strict decoder. Explicit v1.0 migration preserves older opaque route strings. [State contract](09-session-state-contract.md) describes strict decoding. Capture timestamp origins and terminal reasons now live in journal1.1; measured latency, monitor-mode UI and richer processor-tail state remain later work. Device numeric IDs are ephemeral and must not be used as persistent identities.

## Required gates

- Ten-second synthetic capture has exactly480000 frames, matching known input samples, sequence numbers and raw-media hash. Native capture duration/rate measured independently; do not pretend a C++ generated buffer exercised PipeWire.
- In-process EQ response within±0.05 dB; accepted control event audible by next process block plus declared smoothing window ≤20 ms; no preset change needed. GUI-to-engine latency measured separately.
- Live-engine and offline deterministic render max sample difference≤1e-7 using the same EQ/input/automation. Identical tests with blocks16/64/127/512/2048. Clipping only at explicit device/PCM protection stage.
- No GUI/disk calls, ordinary allocations/frees or blocking lock acquisitions in host-owned process path. Test includes event application and graph replacement, not just steady-state DSP.
- Save/reopen restores all IDs, media extents, EQ/routing intent and range. Wrong same-named assets rejected or explicitly confirmed. Repeated save failure preserves previous generation.
- Export frame extent is exact after declared latency trim/tail policy; file validates in independent WAV reader. Output cancellation/failed write cannot replace an existing file. 4 GiB behavior explicitly RF64 or segmented; the old Studio writer limit is not inherited silently.
- Input removal, callback quantum increase, writer queue exhaustion and disk-full have explicit recoverable states, gap counters and durable-prefix recovery. A synthetic failure test passes before physical recording is offered.
- GUI/audio setup can be closed without corrupting a take; quit drains/finalizes disk writer after stopping callbacks. Desktop launcher uses normal privileges.

Completion evidence goes in `tests/results/SLICE-001/` with build/hardware/source manifest. Foundation evidence is not a full-parity certification. The concrete next coding action is **S8 end-to-end native/portability/route acceptance (S7a/S7b core and desktop export now exist)**, followed by the remaining S8/routing/native acceptance gates; remaining S5 hardware latency/reprepare/Windows gates stay open; remaining S4 filesystem/native Windows/>4 GiB gates stay open. Windows/localization remain alongside these milestones; other-suite native import is now explicit X004 with its own [contract](10-project-import.md).

S6d now provides the backend-free playback clock gate and explicit-route PipeWire playback lifetime owner, used by the independent native sink fixture. [The contract](19-native-playback-owner.md) records verification and an unresolved native timeout during overlapping qualification. Connect that owner through a desktop transport/preparation worker next; the GUI recording/playback/export workflow and full first slice remain incomplete.

S6e now connects Linux GUI output selection, playback/stop, live EQ/undo and metering to a separate worker, with accepted/applied revision reconciliation and asynchronous close. [Contract](20-desktop-playback.md). At the S6e checkpoint the GUI could not record or export; S6g below now connects recording.

S6f now provides [production native recording ownership and verified asynchronous raw take attachment](21-native-recording-owner.md). S6g below now connects input/arm/Record/monitor/manual recovery and close choreography; S7 export remains next. Monitoring correction from equipment profiles is still not connected.

S6g now [connects first-track desktop recording, live EQ/undo, verified take handoff and manual recovery](22-desktop-recording.md). Normal close finalizes/attaches before the dirty prompt. The full slice still requires S7 export and remaining routing/monitoring/native/platform acceptance gates. Automatic discovery and M2 multitrack/overdub/Auto monitoring remain open.

S7a now [renders transactional float WAV/RF64 exports through a private shared EQ](23-offline-export.md). Linux debug/sanitizer regression, exact live/oracle comparisons and active signal cancellation pass; Windows headless cross-build passes. This adds worker API/developer CLI only. Desktop range/overwrite/progress/cancel/close controls, S8, native Windows and physical/durability gates remain required.

S7b now [connects desktop snapshot export](24-desktop-export.md), with real WAV-write/immutable-prefix/overwrite/progress/cancel/close evidence. S7a processing/format and remaining filesystem/native/platform limitations still apply. This advances the Linux one-track workflow but does not complete all SLICE-001 gates or any frozen parity row.


S8a adds typed per-channel input/playback/monitor intent, explicit v1.0 migration,
shared route/EQ undo and desktop restoration with named unresolved placeholders.
See [routing contract](26-project-routing.md). This is a portability foundation;
consolidated native record-to-export, hardware, Windows and other remaining first-slice
gates stay open.

S8b adds [saved monitoring preferences](27-monitoring-preferences.md), strict 1.2
state and accepted-prefix recording preparation. Native reopen no longer needs a
manual monitor-mode override. Next: bounded interrupted-recording discovery and
consented verified-copy recovery, then M2 multitrack foundations; platform and
physical gates remain required.
