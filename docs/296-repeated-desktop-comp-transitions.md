# Repeated rendered transitions in the desktop workflow

Checkpoint 296, 2026-10-14. Full DAW goal remains active; this is source
development, not a replacement for the installed preview.

## Usable workflow

The timeline's existing **Save and render** operation now accepts clips backed
by earlier comp assemblies. It prepares owned requests, registers and saves their
references, supervises rendering, assembles, then offers **Review** and a separate
**Apply**. Rendering alone does not change timeline audio. Undo/Redo and project
reopening retain the original and derived media and their provenance.

The temporary repeated-pair rejection in `prepareOwnedCompTransition` is removed.
All downstream original-history validation, immutable files, resource admission,
worker policies and canonical Apply guards remain. This uses the same desktop
controller path as first-generation edits. No new controls, translations or
runtime dependencies are introduced. See [ADR 185](decisions/185-repeated-desktop-comp-transitions.md).

## Source-bound acceptance

At source `8f4df7895b74a82458de319989980d58b6087c7b`, all six selected Release suites
pass in **338.62 seconds**, with source and executable hashes unchanged:

| Suite | Seconds |
| --- | ---: |
| Complete CI partition coverage | 0.18 |
| Clock-aware span workers | 58.32 |
| Existing desktop assembly Apply | 27.12 |
| Existing timeline transition creation | 23.84 |
| New repeated desktop transitions | 229.09 |
| Session state | 0.04 |

The new workflow uses real original assemblies at 44.1, 48 and 96 kHz source
rates. Each rate performs two successive repeated edits and a third edit using a
retained assembled take on one side. All nine adoptions go through the actual
modal timeline dialog, background controllers, review selection and Apply button.
Fifteen successful adoption workers retire normally. Each adoption exercises
Undo/Redo, durable save, reopening, live playback and WAV export. Exactly 508,182
live/export samples agree; 502,422 outer-playback samples remain unchanged.
The additional outer comparison excludes the transition plus 128 frames on each
side. Complete original-file verification still runs. Floating headroom remains.

Five additional 48 kHz cases cover canceled preparation, a source edit while
preparation is held, corrupt original history during rendering, history changed
after Review, and closing Review while Apply is held. None applies pending audio.
Final window close retires the controllers; all eight cases release their joined
assembly resource credits after window destruction. Duplicate render and Apply
requests are refused. These are owned synthetic tests, without audio endpoints.

Three existing first-generation export hashes remain exact. The six paired
repeat exports also match the preceding core qualification. The three mixed
exports are **different fixtures**: the desktop test restores an earlier assembled
incoming clip with its saved fade; the earlier core test restores the resolved
original nominal clip with its stretch settings and no fade. Their export hashes
differ, and are not claimed equivalent. Each independently passes unchanged outer
playback and live/export agreement. This is consistency evidence, not independent
processing-quality admission.

## Evidence and visual follow-up

[The evidence index](../tests/results/M2/2026-10-14-comp-repeat-ui/index.json)
records exact source, binaries, commands, results and original corpus paths.
`acceptance.zip` preserves 4,150 original files through 929 distinct payloads:
14,147,423 bytes, SHA-256
`bea03a6b886cc189ae711458e4fc10007908fab6026aff4619bdde63380868db`.
Original names map to payloads in `files.json`; every payload digest and ZIP CRC
was verified. No preceding archive was replaced.

The initial focused 48 kHz run passes all six cases in 78.83 seconds. Its first
two builds exposed missing private test include paths for the Windows path helper
and pinned libsndfile header; those include paths were added. Failed build logs
and the initial smoke logs are preserved.

Visual inspection found a test capture taken immediately after changing the
button text, before Qt had processed its layout update. After the source-bound
run, the test was changed to process pending events and assert that the button
is at least its size hint before capturing. This is a **test-only follow-up**;
production source is unchanged. A fresh 44.1 kHz desktop run passes all three
adoptions in 70.46 seconds, produces the same three export hashes as the full
qualification, and shows the complete Save and render caption. Header and binary
hashes are retained. The settled dialog and applied-window images were inspected.

`supplemental.zip` retains the visual follow-up's full corpus, hashes, diagnostics,
initial smoke logs and the explicit core/desktop export comparison:
4,506,386 bytes, 278 files, SHA-256
`3ef4fe751d73f1440483bba06826dc1cfd0a8e2280a446a82112e6ded8a27e32`.
The earlier full qualification is not represented as having executed this final
test-only capture adjustment.

## Remaining gates

The original preceding CI run 38664035143 at `42813e4` was still active when this
slice was qualified. The Windows cross-build had passed; native Windows and
Linux execution were pending. Its terminal APIs, full logs, all original ZIPs and
screenshots must be retained and audited before another PR head advance. Current
source then needs fresh native Windows and Linux CI. No unchanged rerun,
cancellation or deadline relaxation is included.

The previous Debug worker's narrow 9.9911/10-second margin remains relevant;
passing this Release desktop suite does not resolve portable performance margins.
Installed preview delivery, independent sound quality, nonuniform/protected
rendering, additional channel/track layouts, native audio and full European
localization remain open. All M2–M11/F/Q/C/N/X004/X005 requirements remain active.
