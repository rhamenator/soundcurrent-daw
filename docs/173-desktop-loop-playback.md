# Checkpoint173: saved desktop loop playback

The transport exposes **Loop playback**, **Set loop range…** and the saved range.
Positions use inclusive start and exclusive end in project sample frames. Enabling
an empty saved range initializes ten seconds at the playhead, with overflow
refusal. Stop transport preparation/playback before changing the range. Disabled
loops preserve locators; Save/reopen preserves intent without activating a device.

## State and history

Schema1.17 adds required `loopPlayback` with boolean `enabled` and signed integer
`startFrame`/`endFrame`. Values must be nonnegative and ordered; enabled ranges
must be nonempty. Missing/unknown keys, fractional/boolean positions and unsupported
schemas are refused. Schemas1.0–1.16 migrate to disabled zero locators; they reject
new keys. No old project is overwritten merely by opening it.

`SetLoop` is a semantic structural edit. History records and conflict-checks its
before/after values, including grouped edit refusal and identical-value no-op.
Undo/Redo, budgets and canonical snapshots use the existing control owner. A
loop edit never alters media bytes, parameter IDs or audio-route intent.

The GUI range dialog parses localized whole-frame values with `QLocale`, exposes
keyboard/buddy/accessibility labels and rejects obsolete project epochs. Controls
are disabled during active/prepared transports, unresolved manual takes, close,
export and project I/O. This generation-based edit policy is partial: live loop
locators/toggle scheduling and seamless graph replacement remain future work.

## Playback and native adapters

Preparation copies the saved region to `PlaybackConfig.loop` and then to the
shared `MixPlaybackConfig` in both PipeWire and WASAPI desktop endpoint adapters.
The prepared playback stream ends at the representable clock limit rather than a
fixed pass count; storage remains the existing bounded cache/slab/graph state.
Normal user Stop retires native playback before reader cleanup. A pathological
near-limit playhead still refuses an empty stream.

The playback snapshot exposes stream position, wrapped project position and pass
ordinal. The UI displays the one-based pass and project frame separately from
stream time. Scalar EQ changes retain the running state. Changing saved loop
intent through another controller is a structural mismatch requiring retirement,
not an implicit live retime. Stretch-result audition uses its explicit one-shot
selection instead of the project's loop. Ordinary WAV exports retain their
explicit existing range; a saved playback toggle does not silently repeat exports.

The manual playback/monitor preparation also uses this mapping within its existing
finite duration and reports passes. Its manual In/Out commands still use the
monotonic stream clock. They do not automatically create a separate take per lap.
The ordinary fixed-duration record workflow remains a separate linear workflow.
Automatic loop recording and explicit pass-preview placement require the scheduler
below; this checkpoint does not qualify that reference workflow.

## Acceptance and platform scope

Core state tests cover strict codec, legacy neutral migration, grouped refusal,
semantic Undo/Redo and disabling without locator loss. The desktop focused
workflow creates actual owned raw media, uses the real project controller and
widgets, enters invalid/valid localized positions, performs keyboard Undo/Redo,
Save/reopen, prepares loop playback, checks pass/project status beyond the original
clip end, blocks active-generation edits, stops, disables and prepares linear
playback again. Source hashes and explicit route activation are checked.

The GUI workflow uses a simulated endpoint, so it qualifies preparation/status/UI
behavior without native audio or listening claims. Shared reader samples, fades,
physical-rate fractions and delayed five-pass capture are independently covered
by [checkpoint172](172-loop-playback-recording.md). Windows CI now explicitly builds
and runs this same GUI/controller workflow; Linux success does not qualify Windows.
Source-bound outcomes are retained separately. The local screenshot is a layout
inspection, not an installed or native audio test.

Localization adds source keys to all34 partial catalogs with source fallback for
unfinished translations. No new native-speaker or complete-UI language claim.
Full European register/translation/review/UI qualification remains required.

## Next: automatic recorded passes and qualified preview

Drive pass scheduling from the same project/stream mapping, prepare upcoming take
slots off RT, preserve latency postroll across wraps, and retire/replenish joined
slots with explicit budgets. Retain every complete/partial/unconsumed preview
receipt. Exhaustion must report backpressure before losing capture or overwriting
raw takes; the eight prepared slots must not become a session take-count limit.
Place retained pass clips at their original project loop origin and preserve
stream/device provenance. Qualify actual GUI plus owned native Linux/Windows loop
workflows independently before installed preview promotion.

Audition/comp crossfades, project automation/tempo/MIDI repetition, live locator
edits, external/hardware/load/interruption gates and all frozen-reference submodes
remain open. F/Q/C/N, M2–M11, X004/X005 and European delivery remain incomplete;
existing stretch-quality failures remain unchanged.

## Source-bound local result

Frozen source `0e65fc33d8e89df60f2fd50360bcd61cdadc8af9` passed19 distinct
Linux suites in two sequential cohorts:18 suites in41.06s and the separately
selected `manual-desktop-control` suite in0.75s. The focused simulated desktop
workflow reported16 checks; the shared-reader loop test reported32129 checks,
five actual two-input recorded passes and callback allocation/free/blocking-lock
counts of zero. These are separate synthetic/controller gates, not19 native tests.

The [result packet](../tests/results/M2/2026-10-11-desktop-loop-playback/summary.json)
contains a31,754-byte bounded ZIP of eight text/JSON members,498 source-file
hashes and local executable hashes for17 built targets. The standalone verifier
checks packet consistency; optional `--source-dir` explicitly compares the frozen
source bytes. Both modes have no Git-history dependency, code/audio replay or
signature claim. The frozen source comparison passed locally before evidence was
committed. Earlier failed development runs and an empty test selection are not
admitted. No executable, installer or release was uploaded.
