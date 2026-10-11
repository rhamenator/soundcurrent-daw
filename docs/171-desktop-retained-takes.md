# Checkpoint171: desktop retained takes and explicit region comping

This is a partial M2 workflow. It does not qualify loop transport, transitions,
take audition, MIDI takes or complete frozen-reference parity.

## Usable workflow

1. Record and stop a complete manual punch group. Choose **Retain as take lanes**
   in the recording preview. The existing **Add complete take** action still
   adds audible arrangement clips. Retention can also be requested while the
   manual transport runs; comp edits require stopped transport.
2. Select a track and its **Retained take** in the timeline. Lanes remain silent.
   Name and order are editable. The chooser borrows immutable session state,
   formats only requested rows and charges its index storage to GUI resources.
3. Set an inclusive start and exclusive end in project frames. Choose **Use take
   region**. Enable the recorded-group option for one atomic selection of all
   matching tracks. Every selected lane must cover the range. The desktop's
   existing 64-operation transaction bound applies; this is not a total take or
   track inventory limit.
4. Select an arrangement clip to see its retained source. Undo/Redo operates on
   the entire selection. Save/reopen preserves lanes, source IDs and names.
   Stop and reprepare playback to hear a changed arrangement; this slice does
   not replace a running arrangement graph.

A referenced lane cannot be removed. **Remove unused take** removes its session
lane with Undo support; owned media files are preserved. Retained source names
are Unicode strings, while identities and project state remain language
independent. Schema1.16 and the original comp engine are documented in
[checkpoint170](170-retained-take-lanes.md).

## Ownership and failure behavior

Retention is a distinct complete-group attachment intent, bound to project ID,
root and epoch. Shape/resource admission occurs on the control worker. The I/O
worker re-inspects finalized journals and verifies media before publication.
No speculative asset or audible clip is published. Failure leaves the preview
available. An intervening scalar edit is preserved when constructing the final
candidate, and the existing snapshot/history preflight is repeated before one
canonical publication. No Save is implicit. The audio callback path is unchanged.

Take-list preparation participates in the timeline's staged projection admission.
Failed preparation retains the previous rows and resource credit. Clip selection
preserves the take selection; changing project epoch or track clears it.

## Acceptance and qualification scope

`sc-manual-panel-tests --retained-takes-only` uses the actual capture writer and
joined synthetic manual endpoint, without a native endpoint or VM. It records
two two-track groups, verifies raw samples, and checks silent retention in live
playback and float-WAV export. A trimmed grouped comp is compared with independent
captured-frame values, 13/256-frame partitions and offline export. Raw file hashes
are unchanged after editing and reopen.

It also exercises obsolete-epoch/incomplete refusal, missing finalized-media
refusal and restored-media retry, edits during held verification, one-step
Undo/Redo, repeated-selection identity/history preservation, selected-source
labels, Unicode rename, reorder, referenced-lane refusal, unused-lane removal
and Undo. A separately declared 131-lane projection checks on-demand row access
and exhausted-budget refusal; these are alias fixtures, not 131 recording runs.
Callback allocation/free/blocking-lock counters are checked at zero.

The focused test and eight desktop regression suites pass locally during
implementation. The final source-bound capture and its exact test outcomes are
stored under `tests/results/M2/2026-10-11-desktop-retained-takes`. Native Windows CI
now explicitly builds and executes this workflow; Linux success does not qualify
Windows. Catalogs contain 877 source keys across 34 partial catalogs, with no
native-reviewed or fully UI-qualified languages. New unfinished translations
retain source fallback. Full all-Europe coverage remains open.

## Next implementation task

Implement real loop timing and pass retirement through the existing native/manual
owners: monotonic device/capture clocks, separately looped project position,
inside-block wrap, latency postroll and bounded prepared slots/backpressure.
Do not treat the existing continuous manual punch clock as loop playback.
Then qualify take audition, comp crossfades, linked edits, physical/native/installed
workflows and reference-specific suboptions. Existing stretch-quality failures
and full M2–M11/X004/X005/F/Q/C/N gaps remain visible.
