# Visible transition render stages and elapsed time

Checkpoint 329 improves the transition-render dialog while retaining its existing
render, cancel, review and Apply workflow. The previous dialog described the
entire source stage as rendering, including time spent checking previous history.
That was insufficient feedback for the prolonged Windows operation observed in
[checkpoint 325](325-installed-windows-portable-comp.md).

## User-visible behavior

The dialog now distinguishes checking source/history, rendering source spans,
verifying source audio, loading the saved transition, and verifying assembled
audio. These descriptions follow existing immutable controller snapshots; they
are not inferred from file growth or guessed percentages.

Elapsed wall time begins when preparation is accepted and includes preparation,
registration, save, rendering and verification. It uses the GUI's monotonic clock
and the window locale's decimal formatting. It stops advancing when the operation
completes, fails or finishes cancellation. It is not an estimate of remaining
time, an audio clock, or a processor benchmark. The existing dialog refresh timer
is reused; no work was added to the audio callback or disk/render workers.

Render deadlines, verification, publication, consent, cancellation and recovery
semantics are unchanged. Timeline audio still changes only after explicit Apply.
This feedback does not resolve the earlier Windows render-delay finding.

## Verification

The serial low-priority Linux build passes. Three focused CTests pass in 22.02
seconds: `desktop-comp-transition-create`, `daw-translation-catalogs` and
`daw-translation-catalog-failures`.

The actual Qt workflow covers eight cases across 44.1, 48 and 96 kHz, with 228
checks. It exercises public timeline creation, new owned jobs, review/Apply,
Undo/Redo, persistence/export and the existing cancellation, project-replacement,
stale-edit, missing-helper and save-failure cases. The added checks observe
elapsed time advance while preparation is held and remain fixed after successful
completion. Three completed render screenshots are retained; the 48 kHz dialog
was visually inspected with both status and elapsed text readable.

Actual Linguist extraction adds five source strings to the existing 34 catalogs,
bringing the inventory to 1,095 source keys. Existing draft translations are
preserved. New untranslated entries retain English fallback; this adds no claim
of translation review or full European language support.

Evidence: `tests/results/M2/2026-10-15-comp-render-progress/acceptance.zip`,
1,011,282 bytes, 685 entries, SHA-256
`152a6c4fdade4358b9faa514f48994c18381d86930662ec53a9816119b09d23b`.
CRC and all archived payload hashes pass. Reports, screenshots, build/test logs,
source patch and hashes for all 910 generated workflow files are retained.
Generated audio stays local; the manifest distinguishes published files.

No VM was started, installed preview changed, or binary release uploaded. Native
Windows qualification is pending. CI
[38773460845](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38773460845)
continues on checkpoint 327 and must finish with its original evidence retained
before the draft PR is advanced. Checkpoint 328's cleanup change also still needs
native qualification. Continue those checks and useful-preview packaging while
keeping the full DAW, quality, hardware and localization scope open.
