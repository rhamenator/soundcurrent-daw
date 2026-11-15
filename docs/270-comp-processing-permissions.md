# Comp processing permissions derived from the requested transition

Checkpoint 270 adds an immutable control-side plan for the crossfade's allowed
processing region. The plan retains the existing handle envelopes and their
project/root/epoch/expected-source checks. It does not access files, adopt media,
change the project schema or qualify an edge renderer.

`prepareCompProcessing` derives each fragment's permissions from the actual
`PrepareCompTransition`, clip playback map and maximum requested project blend
width. Existing media that already supplies handles requires no new processing.
The width is limited by the requested transition's overlap with the old fragment.
Both fragment snapshots and permissions retire together under ledger credit.
Stale model/context checks and moved-from refusal preserve immutable ownership.

## Resampling footprint and processing domain

The plan first maps the transition into expanded-artifact coordinates using exact
source timing. A physical source-center interval alone is insufficient: the FIR
can mix modified neighboring samples into project output outside that interval.
For physical processing, the permitted range is therefore tightened by the full
prepared read footprint of the nearest audible sample before/after the transition.
Both newly generated edges and the inner blend must fit that protection.

When physical processing cannot retain the requested width safely, the plan
selects `ProjectAfterSrc`. The original and candidate streams must first be
resampled independently; blending then changes only the permitted project frames.
That path uses project-domain current/prefix/suffix counts and explicit permissions.
It is a required path for general rates/fractional origins, not an excluded feature.
The plan does not silently shorten the blend merely to obtain physical eligibility.

One-frame processing regions now have a defined complementary half-weight in the
blend primitive. They are not widened. Boundary slope observations remain absent
where a side has only one sample, and processing-quality admission remains separate.

## Source-bound acceptance

[Frozen receipt](../tests/results/M2/2026-10-14-comp-processing-permissions/index.json)
binds implementation `247bdaa4b9c951f21b37d27464638bff530647d2`, source/library/
executable hashes and actual CTest process3741695. Three selected Linux gates pass
in1.83 seconds on a dedicated Release core build; older qualified caches are intact.

The new 33,054-check corpus exercises source rates44.1/48/96kHz, project rate48kHz,
speeds1/1 and3/2, whole/fractional playback origins, physical offsets0/64 and short/
long requested windows. It covers51 physical and35 project-after-SRC fragment
cases, plus existing-media handles. The tests use actual positioned FIR processing
and assembly on owned analytic PCM and compare every original fragment output
outside the transition bit for bit. This is a PCM scope proof for the stated
software corpus; it is not native audio or an acoustic renderer-quality test.

The corpus also checks one-frame permissions, maximum-width/resource refusals,
unchanged canonical state, stale epoch/clip rejection, immutable moves and credit
retirement. Strict assembly's37,098 checks pass; the extended one-frame blend
corpus passes5,561,601 checks through256 channels. The source-bound archive contains
logs and complete input/binary/process receipts:21,562bytes/four entries, SHA-256
`d0eb1172a10c7d01f94e5b3ce5a0ebd4e80130384d5046cae075b48ee138afab`.

## Original hosted scoped-blend run

[Run38572570686](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38572570686)
against9dece84 passes all four jobs: Linux172, Windows core cross-build, Windows
native93 plus four explicit core gates, and desktop22. The corrected Windows media
UI test passes75 checks. All original terminal APIs/full job logs/five artifact
ZIPs are retained before the PR update. Independent build-input review verifies
14 payload files,1554 corresponding source files with actual Windows checkout
filters, collection hashes and both pinned media DLL builds. Main-window/native-
audio/installer flags remain false. Earlier failed-run evidence remains retained.

The new permission/one-frame source is included in native Windows CI; its own
execution remains pending. No product UI source changes or preview replacement.

## Next work

Connect immutable permissions to a versioned processing recipe and source-bound
job/completion/state identity, then independently verify permitted differences
and untouched regions before adoption. Project-domain processing requires either
a prepared post-SRC overlay shared by live/offline paths or an explicit new render
representation; it must not be forced into the old same-rate window schema.
Preserve original raw/nominal assets, anchors, atomic Undo and recovery.

Continue broader rate/group/native coverage and full sound-quality gates. The
prior blend's mixed artifact observations remain unresolved; permission safety
does not qualify its sound. Worker/controller/root/epoch/cancel/close/recovery/UI
integration and all frozen-suite/Windows-installation/European-language goals remain.
