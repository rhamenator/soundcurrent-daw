# SLICE-001: record one track → in-process EQ → save/reopen → WAV

Status: **next implementation task, not implemented**. The planning task built an isolated synthetic EQ/WAV probe only. This document defines the first implementation contract.

## User workflow

1. Launch from the desktop/app menu. Create a project with48 kHz rate and one mono audio track. Select an available PipeWire input and explicit monitor output; do not change system-wide defaults.
2. Arm the track, select monitoring off/auto/on, and record at least ten seconds. Show elapsed recorded frames, input peak and any gap/error. The captured source remains raw so later EQ edits do not destroy it.
3. Stop; play the take through an **in-process** three-band parametric EQ (frequency/gain/Q plus enable, with persistent band IDs). Move gain/frequency while playing, hear bounded immediate response, and undo a control gesture.
4. Save the project. Close and reopen it. Restore media, track/routing intent, EQ parameters, sample position and rates. Missing devices show a clear placeholder rather than an arbitrary default output.
5. Export the selected range as48 kHz mono float32 WAV through the same EQ graph, with optional PCM24 as a follow-up within the slice only if format fixtures pass. Reopen exported WAV and compare it with deterministic live-engine capture. Correction/dim/talkback remain outside the export path as architecture defaults even before those controls exist.

Acceptance first uses a **synthetic PipeWire test source** owned by the test, without physical output links. Physical microphone/speaker testing is scheduled separately when music/background noise permit it. Current authorization from earlier EQ testing is not a reason to interrupt today's playback during a planning-only task.

## Implement in dependency order

| Task | Deliverable | Verification |
|---|---|---|
| S1: Core scaffolding | Qt-free `sc-session`, `sc-engine`, stable UUID/parameter descriptors, mono layout, frame clock; CMake target boundaries | Headless consumer builds without Qt/device headers; IDs remain stable through rename/order and serialization |
| S2: Session state | Version1 JSON schema, media manifest with hashes and relative paths, one semantic undo gesture, validated atomic save/load | Save temp/rename failure keeps previous valid snapshot; move project directory and reopen; unknown version rejected clearly |
| S3: DSP wrapper | Prepared three-band EQ using audited GPL coefficients, float headroom, sample-offset control events and smoothing; same code offline | Flat/unity/response and block-partition fixtures; +6 dB overs preserved inside graph; allocation instrument; unchanged filter histories across scalar gestures |
| S4: Capture writer | Fixed slab pool/SPSC with sequence/frame extents; disk-worker libsndfile WAV/RF64, journal and finalize | Synthetic known waveform and ramp timestamps; disk-full/queue-full reports; valid prefix recoverable; no file APIs in processing |
| S5: PipeWire adapter | Normal-user native filter ports, negotiated planarfloat/quantum/latency; selected input/output lifecycle | Owned virtual source capture; no hardware/default-route changes; rate/quantum change triggers prepared restart; disconnect shown |
| S6: Qt UI | One track transport/arm/monitor/EQ controls, level meter, Save/Open/Export, keyboard labels and desktop integration | Fits1280x720; wheel changes require intended control focus; undo gesture exact; all main actions keyboard accessible |
| S7: Offline export | Private instance of same prepared graph driven by file range, tail descriptor and transaction writer | Frame count/rate/channel data exact; no overwrite without user confirmation; cancellation leaves no published partial file |
| S8: End-to-end acceptance | Recorded evidence of create/record/EQ/save/reopen/export | All gates below pass, actual device test separately documented from synthetic results |

No clip launcher, VST host, notation, new effects catalog, arbitrary-channel UI or native project decoder belongs in SLICE-001. Those requirements retain their milestones.

## Proposed initial state shape

```json
{
  "format": "soundcurrent-daw",
  "schemaMajor": 1,
  "schemaMinor": 0,
  "projectId": "UUID",
  "sampleRate": 48000,
  "tracks": [{
    "id": "UUID",
    "layout": {"kind": "mono", "channels": 1},
    "inputIntent": {"backend": "pipewire", "portIdentity": "stable descriptor"},
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

This example illustrates identity and timing; it is not an approved complete schema. Transport/routing IDs, monitor output intent, three band descriptors, processor enable/tail metadata, capture timestamp/latency and journal schema are added in S1/S2. Device numeric IDs are ephemeral and must not be used as persistent identities.

## Required gates

- Ten-second synthetic capture has exactly480000 frames, matching known input samples, sequence numbers and raw-media hash. Native capture duration/rate measured independently; do not pretend a C++ generated buffer exercised PipeWire.
- In-process EQ response within±0.05 dB; accepted control event audible by next process block plus declared smoothing window ≤20 ms; no preset change needed. GUI-to-engine latency measured separately.
- Live-engine and offline deterministic render max sample difference≤1e-7 using the same EQ/input/automation. Identical tests with blocks16/64/127/512/2048. Clipping only at explicit device/PCM protection stage.
- No GUI/disk calls, ordinary allocations/frees or blocking lock acquisitions in host-owned process path. Test includes event application and graph replacement, not just steady-state DSP.
- Save/reopen restores all IDs, media extents, EQ/routing intent and range. Wrong same-named assets rejected or explicitly confirmed. Repeated save failure preserves previous generation.
- Export frame extent is exact after declared latency trim/tail policy; file validates in independent WAV reader. Output cancellation/failed write cannot replace an existing file. 4 GiB behavior explicitly RF64 or segmented; the old Studio writer limit is not inherited silently.
- Input removal, callback quantum increase, writer queue exhaustion and disk-full have explicit recoverable states, gap counters and durable-prefix recovery. A synthetic failure test passes before physical recording is offered.
- GUI/audio setup can be closed without corrupting a take; quit drains/finalizes disk writer after stopping callbacks. Desktop launcher uses normal privileges.

Completion evidence goes in `tests/results/SLICE-001/` with build/hardware/source manifest. This initial evidence is not a full-parity certification. The concrete next coding action is **S1+S2: create the Qt-free one-track session/parameter model and atomic state round-trip fixtures**, then add S3/S4 before device playback.
