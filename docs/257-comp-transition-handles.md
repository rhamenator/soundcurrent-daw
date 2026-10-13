# Retained source handles for comp transitions

Checkpoint 257 advances P006/P009 beyond editing an existing overlap. It adds
explicit preparation of overlap between adjacent selected comp fragments. Full
reference, listening, native/installed and broader comp qualification remain open.

## Workflow

Select the outgoing arrangement fragment and open **Crossfade clips…**. Choose
the incoming fragment and enable **Use retained-take handles**. For adjacent
regions the dialog proposes a five-millisecond transition, bounded by the visible
pair's outer edges. Adjust its project-frame start/end and paired curve, then apply.

This is one history transaction: preparation extends the outgoing end and incoming
start into their retained audio, then sets both fades. Cancel is passive. The
option must be enabled explicitly; adjacent fragments cannot silently acquire
new overlap through the ordinary paired-fade edit. Missing handles are refused
rather than padded, borrowed from another take, or replaced by silence.

## Exact source and state behavior

`PrepareCompTransition` operates on the canonical control owner. Both fragments
must retain valid source-take identities and form an ordered adjacent/overlapping
pair. Their outer visible edges bound the requested transition. It translates the
incoming source map backward with the existing exact rational timing API and
checks first/last sample coordinates against each retained source range. The
existing portable wide-integer comparator prevents fractional precision loss.
Playback rates and different physical/project rates are supported without rounding.

Only geometry and the paired fades change. Fragment IDs, asset/take identities,
gain, polarity, mute, rate and existing source samples remain intact. Moving the
incoming start translates its opposite fade anchors so their project-time phase
is preserved. Original lanes/media are untouched. State uses the existing codec;
no migration, new media or callback ownership path is introduced. Temporary clip
copies participate in the control-side staging budget. Group edits and resource
refusals retain atomic rollback.

The same-asset rule also permits handles already present in a retained rendered
take. A fragment referencing a separate stretch derivative explicitly requires
preparation of matching rendered handles first. This command does not invent
missing derivative samples or discard stretch/warp metadata. That asynchronous
render/admission workflow is remaining work, not excluded product scope.

## Acceptance

Core tests cover source preservation at 44.1/48/96 kHz with rational speeds,
fractional origins, opposite-fade timing, state/Undo/Redo, repeated no-op,
large-fraction ordering, invalid/self/missing-source/range rollback, eight-track
atomic rollback and history-budget refusal.

The recorded-file tests capture five distinct original mono takes, select five
phrases, and prepare four transitions in one batch for each curve law. Independent
sample calculations match the entire output. Callback partitions 13/127, offline
WAV and reopened playback agree. Float headroom and every original recording hash
are preserved; undoing the transitions and selection returns to inactive takes.
A separate eight-track alias fixture verifies original per-track offsets and
shared transition timing against recorded audio. This is not eight physical mics.
The combined retained-take corpus passes 11,161 checks with callback allocation,
free and blocking-lock counters zero.

The actual Qt dialog corpus passes 62 checks, including explicit opt-in for an
adjacent pair, displayed default/custom ranges, overlap creation and one-step Undo.
Qt Linguist extraction is regenerated: 1,034 keys/34 partial catalogs, zero
native-reviewed or fully UI-qualified languages. New unfinished text falls back
to source; all-Europe remains incomplete.

Native Windows execution for this new command/option is pending CI. No VM,
physical endpoint, installed preview replacement or binary release upload occurs
here. Original PR190 CI passed all four jobs (Linux 163, Windows native 89,
desktop 21, cross-build); its original artifacts are retained, and its dependency
bundle has no mismatches. Those preceding-source results do not qualify this edit.

## Remaining parity work

Take audition, persistent/draggable links, reference-specific defaults/splines,
automation/live graph replacement, rendered derivative handle preparation and
native/installed/listening workflows remain required. The five-take file workflow
is concrete functional evidence, not a claim of complete P006 or processing-quality
parity. Existing stretch-quality failures stay visible. Continue take audition and
rendered-handle preparation while the full M2–M11/F/Q/C/N/X004/X005 goal stays active.
