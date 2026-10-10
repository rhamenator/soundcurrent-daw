# Installed Windows pitch and stretch workflow

2026-10-10 UTC. This bounded acceptance extends [installation and startup](140-windows-installed-stretch-startup.md)
for the unchanged local unsigned preview from source
`f9a63532685c988b4d7203da537957cab41dff92`, tree
`742b786db890a67bbefddcdac8bdb6125d2a481e`. The setup SHA256 remains
`75af398f0c2cbbc36c0f236311745e89f34321ef59e9cde149da7d80dbefe302`.
No product binary or release was uploaded.

## Actual workflow

The warmed full independent Windows acceptance clone ran alone with four virtual
CPUs and 6 GiB RAM. Installed main PID 9136 opened the owned Unicode project
`project-été-Κиїв`, using the installed Qt, platform plugin and libsndfile rather
than a developer SDK. Its selected clip starts at raw source frame 17 + 1/2 and
contains 8,192 project frames. The real dialog rendered duration 3/2 with independent
pitch +7.00007 semitones and preserved formants. The verified result was reviewed
before explicit Apply; the resulting clip contains 12,288 frames.

A separate bounded observer captured actual installed helper PID 2236, its path,
session and executable SHA256 matching the packaged helper. Its observed libsndfile
module also came from the installed slot. Process-start tracing was denied to the
non-admin test account; the retained polling observation establishes the helper
identity, but does not establish a complete process-start event trace or command
line. The observer stopped after normal application exit.

Actual UI Undo, save, Redo and save produced retained project snapshots. Undo
semantically equals the initial project, while Redo equals the applied saved state.
Raw audio and the original fractional source anchor remain intact. The first real
WAV export completed, then the main closed normally with exit 0. A separately
launched main PID 1880 reopened the saved project and exported again after explicit
confirmation to replace the owned synthetic export. It also closed normally.

## Independent audio comparison

| Observation | Result |
| --- | --- |
| Raw source | 12,000 frames, 48 kHz stereo float; original hash unchanged |
| Derived audio and each export | 12,288 frames, 48 kHz stereo float |
| Derived/export samples | Exactly equal, all finite |
| Internal/export peak | 2.226808547973633; float headroom preserved |
| Repeated export files | Byte-for-byte identical, 98,392 bytes each |
| Export SHA256 | `cf7378b79ee37642b0a8591ecc5221a6b57f9f83915820206ef461a78a4dd76c` |

The independent decoder checks RIFF/RF64 sizes, IEEE-float formats, extensible
format subtype/channel mask, frame counts, finite samples and the completion
marker's sample/file hashes. The first comparison harness expected plain format
tag 3; the helper correctly writes IEEE-float RF64 with WAVE_FORMAT_EXTENSIBLE.
Updating the decoder to accept and validate that format resolved the harness
error. No product change was required. Finite equal PCM and this one workflow do
not qualify the full pitch/stretch processing-quality target.

## Removal and reinstallation

Actual normal uninstaller PID 6108 exited 0. The owned payload, registration and
both shortcuts were removed. A user sentinel file inside the preview slot, a
settings sentinel, and all eight project-tree files survived unchanged. Actual
same-build installer PID 8856 exited 0; all 64 restored payload hashes,
registration and both shortcut targets passed. Project bytes and sentinel/settings
remained unchanged. The export outside the project tree also matched its earlier
capture independently. Post-reinstallation startup and normal close passed; no
additional project-open or render is claimed for that final process. Windows
reused PID 1880 after the previous process terminated.

The clone was shut down normally. All VMs are off; the exact temporary firewall
rule and bounded receiver were removed. Template disk-chain metadata, firmware
hash and normalized configuration hash remain unchanged. An initial ten-second
shutdown observer expired while Windows was still shutting down; a subsequent
state read confirmed it was off. Original VMs and equalizer repositories were
preserved.

## Evidence, limits and next task

[The qualification receipt](../tests/results/M2/2026-10-10-windows-installed-stretch-workflow/qualification.json)
binds a 59-member capsule containing process reports, owned project/audio/export
snapshots, actual action scripts, UI screenshots and cleanup observations. It
contains no credentials, private transport key or installed binary. The original
builder receipt keeps its earlier false installed/release flags; this later
acceptance has separate scope. Maintainer observations and hashes do not
authenticate an untrusted receipt or replay native behavior.

```sh
python3 tests/results/M2/2026-10-10-windows-installed-stretch-workflow/verify.py
python3 tests/results/M2/2026-10-10-windows-installed-stretch-workflow/refusals.py
```

Eleven positive/refusal cases check scope inflation, SDK paths, helper identity,
fractional source anchors, contradictory recorded UI parameters, Undo state, repeat-export samples, removal-preservation
hashes and immutable builder scope, including consistently recomputed ZIP manifests.
Both commands run in the Linux CI retained-evidence step, without native replay.

Next broaden short-span, transient and rate/pitch quality acceptance, then develop
explicit tempo/warp workflows with shared playback/export timing and preservation
evidence. Physical/native recording reliability, refreshed Linux acceptance for
later source changes, full processing quality, content, project compatibility and
European translation delivery remain open in the staged backlog. This checkpoint
qualifies a usable installed editing/export workflow, not full DAW parity.
