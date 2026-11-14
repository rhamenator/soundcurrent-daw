# Original sources for repeated comp rendering

Checkpoint 288, 2026-10-14. The full frozen professional DAW goal remains active.

## Implemented boundary

An adopted assembly now has a control-side source resolver that follows its
saved history to the original raw recording and nominal processor. It captures
the paired dependency closure, retained takes, assets and trusted selection
references in an immutable object with joined resource accounting. Relevant
source changes invalidate it; an unrelated project title edit does not.

The resolver maps current artifact frames to original raw frames using exact
fractional arithmetic. It separately records historical playback speed already
baked into audio and the original/current sample-clock ratio. Current playback
speed and original nominal pitch remain separate. This prevents a future renderer
from accidentally treating assembled PCM as the raw recording or applying an
earlier speed change twice. [ADR 173](decisions/173-comp-regeneration-source-history.md)
describes the clock conventions and ownership boundaries.

This API performs no file verification, generates no new audio and grants no
adoption authority. Existing renderer refusals for assembled inputs remain in
place. Repeated rendering is not yet available in the application.

## Qualification

Implementation `2bd61e80ba8daf9960e0d4d7b7a7645a6d79b557` passed both
`comp-regeneration-source` and `comp-handle-envelope-plans`. The source-bound
qualification took 27.74 seconds, with sources, executable and library hashes
unchanged. CTest exited successfully; all observed child processes were reaped.

- Real owned nominal/span workers and assembly/adoption generated three saved
  fixtures at raw rates 44.1, 48 and 96 kHz. Both fragments in each fixture were
  resolved: six actual adopted sources, all on the 48 kHz artifact clock.
- 101,358 fractional positions match the recorded original-source map exactly.
  Exclusive endpoints, beyond-end refusal, paired closure, shared retained-take
  deduplication, move ownership and memory release are checked.
- Nine additional generations exercise cropped histories, already baked speed,
  physical-artifact stages, canonical save-format roundtrips and accumulation
  of clock/pitch ratios. These are metadata cases with synthetic hashes, not
  additional rendered audio.
- Stale clip, owner/root/epoch, asset, history, selection reference and retained
  take changes are rejected. Missing nominal sources, corrupt maps, insufficient
  resource credit and invalid map limits are rejected.
- The underlying first-generation fixture still passes adoption, Undo/Redo,
  save/reopen and live/export equality. That does not prove these workflows for
  repeated rendering.

The resolver reports 105,271 assertions. The [acceptance index](../tests/results/M2/2026-10-14-comp-regeneration-source/index.json)
binds the implementation and all 623 archive entries. The ZIP is 17,209,771 bytes,
SHA-256 `d348cb34c8ca0331f4c493b8811724ee761b84d5a752990b940381614a7e4a16`.
The [development record](../tests/results/M2/2026-10-14-comp-regeneration-source/development-index.json)
retains the initial incorrect two-take test expectation and its correction.

No production UI files changed, so the preceding actual localization extraction
remains at 1,090 source keys and 34 partial catalogs. Native review and complete
localized UI qualification remain open. This checkpoint makes no new native
Windows, installed application, physical-audio or processing-quality claim.

## Next integration

1. Carry the resolved source and full history through handle planning, canonical
   recipes and stale checks without changing older saved recipe identities.
2. Bind output sample clock and accumulated pitch explicitly in versioned producer
   requests. Preserve old job recovery and verify original nominal processing.
3. Verify actual original media and historical owned assembly jobs before and
   after rendering, including dependency changes during queued adoption.
4. Qualify repeated render/review/Apply, Undo/Redo, save/reopen and export; preserve
   untouched PCM. Then enable the application workflow.

Nonuniform/protected producers, mixed-join and spectral/phase/formant/transient
quality, independent native qualification and the remaining full-suite milestones
remain required. Small previously observed Linux/Windows sample differences are
still open; no new tolerance or quality admission is introduced here.
