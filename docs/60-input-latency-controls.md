# Saved input latency controls

Date: 2026-10-06. P004 / M2 recording alignment; a bounded workflow checkpoint.

## User workflow

Select a track and edit **Selected track input latency** in the Recording inspector.
The value is whole sample frames, with a milliseconds readout at the project rate.
The default is zero; the maximum is 60 seconds. Enter/focus loss commits typed
text, arrow steps commit directly, and an unfocused mouse wheel scrolls the page.
Undo/Redo and Save/reopen preserve each track's value independently. Loading a
project restores preferences passively.

Stop prepared/active recording before changing this setting. Preparation captures
one accepted canonical snapshot, including the declared delay on each armed lane.
The UI disables the control while preparation/commands, recording, pending take
attachment, incompatible I/O/export or shutdown are in progress. A structural edit
from another binding retires an incompatible owner and retains any joined take.
Routes remain explicit after preparation.

For ordinary recording, a raw take starting at frame S with declared delay L
attaches at S−L. If that is negative, attachment starts at zero with a source offset
of L−S; the media itself remains unchanged. Musical punch uses desired locators
[B,E), captures each lane at [B+L,E+L), then attaches at B with source offset zero.
The engine admits checked postroll independently for the most delayed lane.
This is a declaration, not a hardware round-trip measurement. A wrong declaration
can misalign a take; automatic/device-measured compensation remains open.

## Persistence and control contracts

Schema 1.5 adds required `inputLatencyFrames` on each track. The strict codec
accepts whole integers only, bounds them at the project rate, and rejects missing
or unknown fields and unsigned overflow. Schema 1.0–1.4 migrate explicitly to zero;
newer schemas fail rather than discard data. Stable-ID structural edits validate
an entire batch before publication and share bounded Undo/Redo. Existing routes,
EQ, assets/clips, punch locators and original media are retained.

The state change introduces no dependency or real-time operation. Both selected
capture and shared duplex preparation forward the accepted declaration. New
monitor processing, export processing and existing clip geometry are unchanged.
See [ADR 046](decisions/046-declared-track-input-latency.md).

## Acceptance evidence

The checkpoint receipt records final tests, exact source/binary pins, the native
workflow and limitations: [dated evidence](../tests/results/M2/2026-10-06-input-latency-controls.json).
Core tests cover strict rejection, all five older schemas, 8/48/384kHz bounds,
transaction rollback, stable-ID reordering, no-op edits and Save/previous snapshots.
Controller tests use real raw writers/readers with synthetic endpoints, distinct
4097/17-frame punch delays, prepared retirement and active ordinary-take retirement.
Ordinary takes test both subtraction and source trimming at frame zero. Qt tests
exercise the control, scroll policy, Undo/Redo, per-track selection, passive
Save/reopen, preparation, disabled controls, grouped attachment and canonical save.

The first UI test timed out on a combined assertion of canonical and displayed
Undo state. The original values of each term were not logged, so that run's exact
failed term remains unknown. Code review identified a refresh path where a quick
edit→Undo can return canonical state to the last observed value before a GUI poll,
leaving a changed widget unrefreshed. Retained executable/source/log hashes identify
the initial run. The binding now also compares the unfocused displayed value with
canonical state; subsequent success does not assign causes to the original run
or any of the prior 23 observations.

The final Linux Debug suite passes 29/29 groups (27.09s); its functional checks
ran while a separate sanitizer build was still compiling, so no performance
qualification is inferred. The completed ASan/UBSan/LSan suite passes 29/29
(72.72s). Windows core cross-compilation passes; no native Windows claim follows.

The short isolated 32-track native desktop run then passes, with delays repeating
4097/0/41/200 frames. All 3,072,448 raw and 480,000 stereo output samples match
exactly; all per-lane native origins and desired clip positions match. Live EQ,
Undo/Redo, Save/reopen and grouped attachment remain verified. Peak 3.700823 shows
retained float headroom; callback allocations/frees/blocking locks are zero.
The owner maximum is 0.711047ms at 256-frame/48kHz quantum. Complete elapsed/CPU/
resource/cycle gates pass without overruns. Four pre-existing external links and
default metadata remain unchanged; owned nodes/links retire. Eight altered receipts
are refused. The screenshot was visually checked: disabled input latency shows
4097 samples / 85.354ms beside the prepared punch summary.

The first native run stopped before Record on a fixture saved-state assertion:
it compared the unsaved latency-edited expected model with the original saved
zero-delay project. The original saved project, launch/executable/source pins and
failure receipt are retained. The fixture now retains originalSaved independently;
no production correction was needed for that assertion. Including the first UI
assertion above, 25 observations remain recorded. The zero-delay native mode is
still available, with the same acceptance checks.

Reproduce the declared-delay native workflow only after all owned build/test
producers are terminal:

```sh
cmake --build .cache/build-desktop-release --target sc-pipewire-punch-ui-fixture
python3 tests/verify_pipewire_punch_ui.py \
  --binary .cache/build-desktop-release/sc-pipewire-punch-ui-fixture \
  --tracks 32 --input-latency --output .cache/input-latency-native.json \
  --failure-output .cache/input-latency-native-failure.json
```

## Remaining work

No full P004 or other frozen contract is promoted. Functional, processing-quality,
content and native-project axes remain separate. Physical round-trip latency,
Windows native recording/UI, sustained native deadlines, process-kill punch
recovery, empty preroll, desktop fault/discovery and real disk-full durability
still need qualification. All-Europe translation/review/UI coverage remains open.

Next product task: add explicit manual punch and Auto monitoring semantics with
sample-boundary events, safe stop/recovery and user-visible saved mode state;
then tempo/loop/take lanes and comping. No full-reference capability is excluded.
