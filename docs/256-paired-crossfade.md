# Paired clip crossfade editing

Checkpoint 256. This advances P009 and the transition tools needed by P006.
The frozen reference baseline remains unchanged; full workflow and quality parity
are still unverified.

## Workflow

1. Stop transport and select the outgoing arrangement clip.
2. Expand clip processing and choose **Crossfade clips…**.
3. Select an overlapping incoming clip. The dialog offers their shared range;
   an existing visible paired transition restores its saved range and curve.
4. Adjust the start and exclusive end in project frames. Choose linear equal
   gain, smooth equal gain, or equal power, then apply.
5. Both fades change in one Undo step. Reopen/save preserves their ordinary clip
   fade state. Stop and reprepare playback to hear the updated arrangement.

Cancel changes no project state. Selecting the same clip or a nonoverlapping
peer cannot enable Apply. The canonical edit rejects stale/missing IDs, invalid
curves and ranges not covered by both clips. The editor also refuses acceptance
if the project epoch, selected track/clip, or editability changes while it is open.

## Engine and state

`SetClipCrossfade` is a framework-independent control-side transaction. It sets
only the outgoing fade-out and incoming fade-in, translating one project-frame
range to both clip-local ranges. Gain, polarity, mute, opposite fades, source
positions/fractions, playback rates, media and retained-take provenance stay intact.
Existing session/history budgets and the 64-operation batch admission apply.
An eight-track batch is one atomic history transaction, including rollback if its
last pair fails. There is no additional total-track limit.

The shared live/offline `PreparedClipProcessing` evaluates the existing curves.
Linear and smoothstep pairs have complementary gains; the sine/cosine pair has
constant summed squared gains. A correlated source can therefore peak higher
with equal power. Floating-point headroom is preserved rather than normalized.
The edit adds no callback allocation, lock, I/O or logging path.

The result uses existing versioned fade fields; no schema migration or new media
is needed. This is atomic paired editing, **not a persistent relationship** that
makes later independent clip moves or fade edits follow each other. Such edits
retain the existing per-clip semantics.

## Acceptance evidence

[Indexed original acceptance](../tests/results/M2/2026-10-13-paired-crossfade/index.json)
binds frozen implementation commit `291db2098240e6ffc1997fb32c575be936684275`,
its 1,505 unchanged input files, actual executable identities and four selected
workflow suites. Subsequent documentation/evidence changes do not substitute for
new-source Windows acceptance.


- Core: 2,365 checks cover common anchors, independent per-sample curve laws,
  frame-limit conversion, preservation, save/reopen, repeated-edit no-op,
  atomic Undo/Redo, invalid-range/curve/ID rollback and eight-track transactions.
- Existing multitrack editing regression: 73 checks pass.
- Qt dialog: 49 checks pass for Cancel, Apply, saved-pair restoration, one-step
  Undo/Redo, self-pair refusal, disabled editing during busy transport, and visible refusal
  when transport becomes busy or the project epoch changes inside the dialog.
  A separate rendered-dialog observation adds a saved screenshot check.
- Recorded-file workflow: all three paired curves are compared with independently
  calculated samples from original stereo recordings. Callback partitions 13 and
  127 produce identical PCM; 37-frame offline WAV export is byte-identical to live
  output. Actual save/reopen and playback, raw file hashes and Undo/Redo pass.
  This runs alongside the existing 1/2/8/32/256-channel clip-processing corpus;
  the combined run passes 189,119 checks with zero guarded allocation/free/locks.

The first UI compile used the wrong list-model method name; it was corrected to
the existing optional-ID accessor. The first new file fixture chose a destination
outside the required `exports` directory; the app correctly refused it. The
fixture was corrected without loosening destination protection. Failures and
corrected logs are retained locally under `.cache/paired-crossfade/`.

Actual Qt Linguist extraction is regenerated: 1,031 source keys, 34 partial
catalogs, zero native-reviewed and zero fully UI-qualified languages. New strings
retain source fallback where unfinished. European coverage remains incomplete.
Native Windows compilation/runtime of this new edit and dialog is pending CI.
No VM, physical endpoint, installed preview replacement or release upload occurs
in this checkpoint. The previous reviewed-pin build (PR189) passes all four jobs;
its fresh DLL matches the reviewed pin, and all dependency mismatches are empty.
That is deployment evidence for the preceding source, not this feature.

## Reference trace and known gaps

The [Reference A arranger documentation](https://www.bitwig.com/userguide/latest/arranger_clips_and_the_browser_panel/)
describes paired fade handling. The [Reference B fade settings](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/fades_crossfades_and_envelopes/fades_auto_fades_dialog_r.html?contentId=MqhV0wrIcR2pfMUhdKlfhw)
describe equal-gain/equal-power behavior. These living pages were consulted for
semantics; they do not replace the frozen patch-version baseline or prove its
exact defaults and suboptions. Algorithms here are original use of elementary
curves, without proprietary code or assets.

Automatic extension into source handles for adjacent comp fragments, take
audition, draggable linked handles, independent/custom spline shapes, defaults,
presets, persistent links, full grouped comp workflows and running-graph
replacement remain required. Existing stretch-quality gaps are unchanged.
Next implement explicit, undoable comp-transition preparation from retained
source ranges, then apply this paired editor and qualify five-take workflows.
The full M2–M11/F/Q/C/N/X004/X005 and localization goal remains active.
