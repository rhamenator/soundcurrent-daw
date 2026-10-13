# Prepared rendered handles and responsive crossfade controls

Checkpoint 260 advances retained-take comp editing. Full DAW parity remains active;
this is the first correction for rendered-handle admission, not the completed
asynchronous generation of missing handles.

## Already prepared derivatives

`PrepareCompTransition` previously required the arrangement and retained clips to
reference the identical asset. This rejected valid independently rendered audio
whose existing file already covered the requested handles. A compiled baseline
model probe reproduced that refusal before the source change.

The command now accepts a declared stretch derivative of the retained source,
or a separate render sharing the retained processed take's original source.
Existing asset IDs, hashes, stretch/warp settings, contexts, raw anchors, render
keys and take provenance remain unchanged. The command creates no audio and
changes only geometry and the paired fades in the existing atomic transaction.

Coverage is checked in absolute physical source coordinates. Exact inverse
stretch/context/protected-warp mapping includes the fractional source origin.
Both proposed endpoints must lie inside the retained source interval. For a
retained processed take, its own visible window is mapped back to that same raw
coordinate domain. A derivative sample may lie between the final physical sample
and the exclusive retained interval end; it must remain below that end. The
existing same-asset rule retains its previous strict sample-coordinate bounds.

`addSourcePosition` promotes the existing overflow-checked warp sum into shared
control-side arithmetic. The warp implementation uses that function rather than
a second copy. Addition canonicalizes fractions and refuses unrepresentable frame
or denominator sums. These lookups run on the control owner, not in callbacks.
No dependency, project field, schema migration or processing algorithm is added.

A missing artifact span, a different raw source, an uncovered retained window or
an unrepresentable coordinate is refused. Existing session validation also rejects
an invalid take/source relationship before the edit. It has not been weakened.
Audio-file verification remains the existing render/admission and prepared reader
responsibility; a model declaration alone is not evidence of actual rendered PCM.

## Crossfade dialog

The dialog now uses scrollable, wrapping rows, a reflowing explanation and buttons
outside the scroll area. Its initial size is bounded by available display space.
Actual default and 350×240 dialog captures demonstrate readable explanation text;
vertical scrolling exposes longer content without horizontal reading at that size.
No reviewed-language, native Wayland or all-display qualification is implied.

Real Qt Linguist extraction/compilation is regenerated after the final UI change:
1,038 keys, 34 partial catalogs, 3,168 drafts, zero native-reviewed or fully
UI-qualified languages. The installed Ubuntu preview remains the separately
qualified checkpoint259 source; this increment is not installed yet.

## Evidence and remaining work

[Source-bound acceptance](../tests/results/M2/2026-10-13-prepared-rendered-handles/index.json)
records the baseline rejection, original observation/fixture failures and final
frozen execution. Core model tests include legacy stretch, explicit context,
protected warp, a retained processed take, independent Fraction expectations,
original visible-coordinate preservation, exact state/Undo/Redo, uncovered-window
and missing-file-span rollback, and overflow refusals. Wrong-source setup is rejected
by the existing session validator rather than used as an invalid edit fixture.

Actual isolated v4/v5 worker outputs receive retained raw provenance before
verified adoption. Their split comp fragments use all three paired fade laws.
Independent whole-PCM checks, multiple callback partitions, offline export,
save/reopen, original/derived hashes and atomic Undo/Redo are verified. Callback
allocation/free/blocking-lock audit remains zero. Metadata fixtures alone are
identified separately from actual worker/file workflows.

The first plain scroll-area implementation still clipped the dynamically longer
label; explicit reflow corrected it. Screenshot review then exposed horizontal
reading at small width despite the height checks. Wrapping long rows fixed that
and added an actual horizontal-scroll assertion. These original results remain
retained. No native/physical audio route, VM, equalizer edit or release upload is
performed by this increment.

Separately rendered fragments with insufficient admitted span still need actual
asynchronous rendering, review and atomic admission. Preserving nominal raw anchors,
source context, timing and markers may require a distinct versioned handle plan;
do not bypass the ordinary `ApplyClipStretch` raw-anchor equality check or invent
samples. Context reframing and warp-end extension need explicit coverage/quality
proof. This difficult workflow remains required, alongside persistent/draggable
links, listening/phase quality and all remaining frozen-reference milestones.

## Next concrete implementation task

Prepare a bounded handle-render plan for both selected fragments, run the existing
isolated worker through `StretchController`, verify both results, and publish one
reviewed atomic transition. Preserve expected clips/project epoch, settings, raw
and take anchors, source context, cancellation, resource ownership and retirement.
Qualify missing-span and changed-project failures before offering Apply. Continue
Windows/installed/native and full-suite work separately.
