# Explicit processing regions for clip pitch and stretch

2026-10-10 UTC, frozen SC-DAW-BASELINE-2026-10-05. This M2 increment adds an
opt-in context workflow to the existing Render → review → Apply dialog. The
[decision](decisions/103-explicit-stretch-processing-region.md) separates retained
raw selection, processing region and visible crop. Existing project behavior and
previous derivatives remain preserved.

## User workflow and exact geometry

Enable **Use neighboring source context** and choose real source frames before
and after the original selection. Controls cap these counts against the original
asset, restore persisted values and ignore mouse-wheel changes without focus.
The helper renders the entire admitted region; applying the verified result is
one undoable edit. Close keeps the render in the background; application exit
still cancels and reaps it. Audition after applying and use Undo as needed.

An original 128-frame clip at physical source position 4096+1/2, with 4095 frames
before and 4096 after, processes 8319 frames starting at 1+1/2. At duration 3/2
its full output has ceil(8319*3/2)=12479 frames. Its visible crop starts at
6142+1/2 and remains **192 frames**, preserving nominal duration and fractional
phase. Split, crop, seek and rerender retain that original raw anchor rather than
recursively processing the derivative. Changing context retimes the crop from
raw coordinates. Arbitrary expansion beyond the selected root interval is not
implemented by this feature.

Context is explicit even at zero/zero. Absent context keeps the original
rounded-output mapping and existing processor/render keys. New keys bind actual
context, nominal ratio and `soundcurrent.stretch-region-nominal-v1`; two ratios
with the same rounded full duration cannot share a key. Separate R3-region and
positioned-copy-region processor IDs distinguish these semantics. Unity copies
retain prepared source samples, including 384 kHz/256-channel signed-zero,
subnormal and headroom cases.

## Admission, compatibility and real-time behavior

Schema **1.14** stores nullable context; 1.0–1.13 remain readable. Invalid fields,
processor/context mismatches and clips outside the retained visible interval
refuse during proposed-state validation. Loading does not regenerate audio or
reidentify legacy R3 unity derivatives. Processor string payload is now also
charged in retained track-edit snapshots.

Protocol **sc-stretch-render-v4** binds explicit mode and both context counts.
Input, output, memory and deadline admission cover the full processing region.
Insufficient real neighbors or a too-short non-unity processing region refuse
before creating an operation directory. No silence padding, automatic extension
or whole-file substitution was added. The child still revalidates source bytes,
uses exclusive jobs and a ready/start handshake, drains finite exact-duration
PCM and publishes a flushed completion. The parent independently verifies both
raw and derived bytes before adoption. Live playback and export use the same
prepared artifact; geometry, disk access, rendering and adoption stay outside
real-time callbacks.

Current package qualification requires **sc-stretch-worker-qualification-v3**,
protocol 4 and twelve actual explicit-region jobs with copy/key/refusal checks.
Only explicitly trusted historical inspection can select old protocols 2/3;
current package defaults cannot downgrade. The retained installed Windows/Linux
previews still qualify only their recorded earlier sources.

## Duration experiment and quality limits

The original [ratio probe](../experiments/stretch-region-ratio/probe.cpp) ran 36
actual offline R3 renders at 8193, 8319 and 8320 input frames, six duration/pitch
settings and nominal versus ceil-adjusted processor ratios. All eighteen
ceil-adjusted ratios drained exactly. Seventeen nominal ratios did; at 8193
frames and 1/4 the nominal route drained 2048 rather than the admitted 2049.
PID 2161724 exited 0 in 3.878 seconds under a 512 MiB address-space ceiling and
60-second deadline. Metrics and exact source/executable/vendor-library hashes
are retained; this experiment does not retain all output PCM.

The product keeps the adjusted full-region drain contract and uses the nominal
visible map. This proves selected duration/phase arithmetic, not acoustic local
alignment. Earlier [context/transient counterexamples](142-short-clip-context-and-alignment.md)
remain unresolved. No automatic context, Q-STRETCH, segmented pitch, warp/tempo
markers or full processing-quality parity is claimed.

## Local acceptance

A fresh, ignored Release build avoids the previously damaged Ninja metadata.
The 147-step focused build and subsequent UI/catalog/inspector rebuild
completed with two low-priority workers. The second build logs12 compile/link steps after CMake regeneration while Ninja
labels its total13. The first retention verifier incorrectly expected13 logged
steps; that assertion was corrected and its original failure is retained. Six
warnings in existing mix/read/UI
code remain recorded; the build does not qualify or resolve those warnings.

Twenty focused workflows pass. The actual helper completed **53 jobs**, including
12 explicit-region jobs, with **383 checks** and the existing **1045-check** shared
artifact oracle. State has **88 checks**; an independent Python Fraction oracle
compares **971 actual C++ cases** (951 accepted, 20 refused) plus four map contrasts.
The desktop workflow has **118 checks**, including actual short-context rendering,
explicit adoption, Undo/Redo, save/reopen, restored controls and exact shared
live/export/split/seek/crop output. Audited callbacks allocate/free/lock zero
objects in the covered paths. The supervisor retains its **48 checks**.
After tightening the current receipt minimum to 383, a separate package run passes
**57 receipt refusals**, including historical-format/protocol rejection.

Catalog extraction covers 808 contextual source keys in 34 partial catalogs.
Existing translations are preserved; new untranslated terms fall back to source.
Native review and complete UI qualification remain zero. This does not deliver
all-European language support. Historical context, unity and installed preview
evidence inspections also pass without replaying an installer or audio device.
[Retained observations](../tests/results/M2/2026-10-10-explicit-stretch-context/)
bind input/executable hashes and raw local logs. This working-source checkpoint
precedes clean-commit CI/native Windows qualification and is not a package receipt.
No VM, physical audio route, installer, binary upload or equalizer change was used.

## Next implementation task

Run bounded owned transient/music/context/asset-edge and fractional-crop quality
comparisons with explicit local landmarks, and retain failures against the
existing Q-STRETCH gate. Qualify a versioned source/output warp-map contract before
adding tempo/marker editing. Current-source Windows/installed/native audio gates
remain independent. Full frozen F/Q/C/N and European language delivery stay open;
the overall completion goal remains active.
