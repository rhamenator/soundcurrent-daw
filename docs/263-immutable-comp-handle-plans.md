# Immutable comp handle planning

Checkpoint 263 promotes the missing-handle geometry planner into the
framework-independent engine. It prepares bounded, immutable metadata for a
paired crossfade; it does not generate audio or admit a new project edit yet.

## Windows font correction: original terminal result

The preceding implementation `e5ab0be7c7a80842d7ad3f46d27f6401bc01cc9d` passed
all four jobs in [run 38483626019](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38483626019).
The native offscreen crossfade test resolves Arial and passes 165 checks. Its
retained 350×240 dialog image contains readable English and requires no horizontal
scrolling. The native recording corpus passes 90 selected suites; Linux passes
165 suites. The earlier recording wait failure remains preserved and unexplained.
A subsequent pass is not evidence of its cause.

All five original artifact ZIPs, terminal APIs and logs are retained. Independent
byte/source review verifies 14 deployment files, 1,512 corresponding source files,
both reviewed media DLL builds and all collection hashes. The bundle is still
marked unqualified for the main application window, physical native audio and
installation. Offscreen unit checks and byte identity do not establish those gates.

## Engine implementation

`CompHandleEnvelope` and `CompTransitionHandlePlan` expose const views of prepared
state. The envelope owns an actual const data object. Copying and move assignment
are disabled; moving transfers the owner, and using a moved-from envelope is
refused. Owned snapshots and map storage retire before their resource lease.

Preparation snapshots project identity/rate, track layout, selected and retained
clips, and raw/base/retained asset declarations. The control owner supplies a
nonzero project revision and bounded workspace identity. Verification requires
that identity and all relevant snapshots still match. The workspace identity is
opaque: this planner performs no filesystem resolution or audio-file verification.
The future controller must bind it to its approved project root and advance the
revision when the project changes.

Frame, estimated output-byte and map-point limits are checked before result
allocation. Existing geometry preserves contexts, protected warp maps, legacy
rounded output ratios, fractional source origins and playback-rate mappings.
Raw fragments can use existing retained coverage without requesting a render.
Context already present in a file is distinguished from physically missing spans.
A second fragment refusal releases the first fragment's reservations. Pair
validation precedes signed end-coordinate arithmetic on malformed project state.

`sourceIntervalRatio` moves the existing exact warp interval calculation into
shared stretch arithmetic. It canonicalizes positive intervals, retains the equal
interval shortcut for large values, and rejects unrepresentable ratios. Both
shipping warp maps and comp handle planning use that function. The prototype's
separate rational implementation is not shipped.

These operations run on the control owner. They allocate, validate state and
acquire resource credit; they must never run in an audio callback. There is no
Qt dependency, disk I/O, render-worker launch, project schema change or live graph
replacement in this increment.

## Evidence

[Source-bound acceptance](../tests/results/M2/2026-10-13-immutable-comp-handle-plans/index.json)
records source `7bf51409c16ac2869621d1151ea1389ce6da7ef9`, input/executable
hashes, the actual test process, unchanged inputs, original logs and oracle
receipts. A fresh single-worker Release build passes eight selected gates in
1.51 seconds: 156 planner checks, 6,807 existing comp-transition checks, 152
stretch-state checks, session/resource tests, and independent timing/geometry
oracles. The warp geometry oracle compares 516 requests and 23,083 checks; the
protected geometry oracle covers 35 accepted/refused requests. Geometry probes
alone do not qualify audio quality.

The planner is included in the native Windows build/test target list.
[Run 38485663885](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38485663885)
for this source is in progress at this checkpoint. Preserve its original terminal
result before pushing further branch changes.

## Next implementation

Add explicit versioned processed-handle window state that preserves the original
nominal/raw stretch anchor, settings, context, warp markers and base artifact.
Then supervise bounded generation through the existing render-worker
infrastructure, review actual owned artifacts, and admit both fragments as one
versioned edit with cancellation, stale-result refusal, Undo/Redo and reopen.
Do not bypass the ordinary `ApplyClipStretch` raw-anchor guard.

Verify original media bytes and independent PCM results, including SRC
neighborhoods and warp/context boundaries. Copying integer base samples is not
proof of unchanged resampled playback. Raw source absence must be refused, not
padded with invented handles. The current and previous installed Ubuntu previews
are unchanged. Full-suite, Windows delivery and all-Europe qualification remain
open.
