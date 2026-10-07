# Manual recording desktop panel

This M2 checkpoint connects the serialized manual controller to actual Qt widgets.
It is a finite-horizon recording workflow, with synthetic functional acceptance;
it does not establish sustained native performance or Windows runtime parity.

## Workflow

In the **Recording workflows** tabs, select **Manual / repeated takes**. Arm tracks,
choose a maximum transport duration and disk-stall reserve, then Prepare transport.
Preparation uses an exact canonical project-command barrier, including previously
accepted parameter edits. It uses the full project, independently of the inspector's
selected track. Prepared channel layout, matrix, arms, input alignment and monitoring
mode remain fixed until Stop and a new preparation. Mixed layouts need a saved
explicit master matrix. Current track/native-channel admission limits still apply.

Select each input and master output explicitly before Play / monitor. Channel
descriptors persist through project history and Save; no hardware default is changed.
Missing or ambiguous saved ports disable Play without deleting the saved intent.
Inventory refresh occurs on the controller worker every250ms, including while
playing. A native endpoint independently validates connections before activation.

Prepare next take allocates its capture reserve on the worker. Punch In/Out requests
apply at the next audio boundary. A worker command completion means submitted;
the separate reliable audio receipt establishes the applied position or refusal.
The panel shows this distinction and blocks dependent gestures while pending.
Repeated takes retain the same prepared playback graph and generation.

EQ changes and parameter Undo/Redo follow canonical revisions through bounded
immediate-event queues. Partial accepted prefixes cannot claim the entire revision
as accepted/applied. The latest desired model coalesces behind outstanding bundles;
audio acknowledgements establish application. Structural changes incompatible with
the prepared inventory/layout/matrix/arms stop safely. Newly adopted clips and clip
Undo/Redo affect canonical state; they are heard after a new playback preparation.
The running graph's original media timeline remains immutable.

## Previews, history and recovery

Joined groups remain previews until an explicit choice:

- **Add complete take** atomically verifies and adopts all complete lanes into
  canonical history. No automatic Save follows. Group Undo/Redo remains available.
- **Add verified complete lanes** requires an uncanceled partial group and selects
  only its independently complete, verified lanes. Failed/empty lanes remain visible.
- **Keep files for recovery** consumes the application preview while retaining
  durable capture files and journals. Recovery discovery remains a separate action.

Verification failures retain the group/files and allow retry. The preview is consumed
only after the project's retained attachment receipt names the expected assets.
Project replacement and structural GUI edits are refused while transport, attachment
or unresolved previews remain. Parameter edits and Save have their existing history
and worker semantics.

Stop is a priority generation signal. Cancel also cancels still-owned recording
results, including during held writer/verifier finalization; it does not rewrite
previously delivered immutable groups. Canceled groups cannot be adopted, including
through the partial action. They retain independently durable recovery prefixes.

Close first requests Stop and waits for native/reader/disk joins. Unresolved previews
require an explicit **Keep for recovery and close** or **Review takes** choice.
Review cancels Close and preserves previews. The usual project Save/Discard/Cancel
prompt follows only after results resolve. Worker shutdown and window destruction
are asynchronous; the pane is destroyed before its referenced project controller.

## Evidence and limits

`tests/manual_panel_tests.cpp` drives actual StudioWindow widgets with a separate
synthetic audio thread and real in-process graph, media writers, verification,
history and ProjectStore. The fake records Synthetic timing provenance. It checks:

- Prefix barriers and rapid duplicate gestures, explicit persistent port choices,
  Ready-state disconnect/reconnect and no implicit activation.
- Three repeated two-lane groups on one graph, exact raw samples, preserved float
  headroom, explicit atomic adoption, group Undo/Redo, Save/reopen and guarded Open.
- Parameter edits/audio acknowledgements and inspector selection without canonical
  track rotation; parameter Undo on the same graph.
- File-verification failure/retry, priority Cancel during held finalization,
  recovery-file retention and refusal of canceled adoption.
- Held Close joins, review/keep choices, no automatic adoption and worker retirement.

The controller test also checks partial event admission, coalescing and generation
high-water isolation. Test-only RT interposition covers the real core callback;
it does not prove every external library/syscall path or processing deadline.
The rendered900×700 offscreen view is a layout check, not physical display or
native-language qualification.

See the dated M2 receipt for exact sources, executables, logs and results. Development
test failures were retained separately; they do not increment the48 retained native
observations. Original48's CPU cause remains unresolved. All92 frozen contracts
remain unchanged and unpromoted. X004 imports, X005 equipment integration, X006
above256-track resource admission, all-Europe localization, indefinite/loop/seek
recording, full take/comp/PDC/plugin behavior and installers remain required.

Next: qualify this exact desktop/controller workflow on owned PipeWire routes,
including repeated takes, parameter application, Stop/Cancel, port loss, failed
adoption and Close; preserve original evidence before diagnosis. Add an independent
Windows endpoint and equivalent native workflow acceptance. Then continue the full
frozen roadmap, including X006 model/parser/mix/UI scaling together.
