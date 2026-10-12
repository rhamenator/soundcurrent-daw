# Native monitoring desktop worker controller

2026-10-11. M2. Implementation `7455b13afc33fbd740c7e4857daca07a3eae84c5`.
Full professional-suite scope remains active and incomplete.

## Implemented controls

`ui::NativeMonitorController` owns one non-RT control worker and the genuine
`NativeMonitorRecording` owner. It exposes immutable snapshots and a bounded
16-command FIFO. Device preparation, project admission, pending publication,
activation, native controls, disk recovery and all joins run on that worker.
Native callbacks never call its factory, mutex, condition variable or UI APIs.
The genuine route, DSP, processing/disk workers and raw writer are reused.

Prepare loads the saved project and binds project/track/layout/unused asset
identity before SDK preparation. It checks a positive, strictly increasing run
generation against the immutable route declaration. Accepted preparation is
copied into owned immutable storage; queued commands cannot observe subsequent
caller mutation of the preparation object. Ready means pending original state
exists and no endpoint has activated. The visible panel is not enabled by this
checkpoint.

Activate requires an explicit positive finite monitor end and submits the original
Stop intent after starting the owned endpoints. This ordinal is not a project
frame or raw alignment. Stop admission and the original planned/refused receipt
remain separate. EQ commands carry a stable parameter address/settings or an
explicit enabled state; the worker prepares events and tracks accepted and
applied revisions independently. The existing DSP's immediate queue supplies
local-frame acknowledgements.

Cancel uses an urgent atomic epoch independently of FIFO capacity. It invalidates
queued transport and acknowledges after native/processing/disk owners join and
settlement completes. Close cancels active work, joins on the control worker and
preserves the terminal outcome. A controller destructor requests shutdown and
joins its control worker; GUI close should first request shutdown and poll Closed.
It must not destroy a still-active controller synchronously from a frame callback.

Terminal results remain available until an exact result-sequence release. New
preparation cannot overwrite them. Inactive Inspect independently verifies saved
native/raw state; it performs no automatic project attachment. Original raw PCM
can contain nonfinite samples and lacks verified project alignment, so a raw
prefix is not silently converted into a playable Asset.

A new fixed terminal-failure query reads processing/disk mutable faults only after
acquiring the corresponding worker's done publication. The controller can then
cancel/join after real processing or disk failure. A normally completed raw disk
worker does not cancel a monitor that is still draining. Missing original native
end receipts remain missing/OwnerLost after cancellation; no native Stop or
timestamp is manufactured.

## Native Windows selection

The opt-in genuine factory refreshes WASAPI inventory on the worker, revalidates
explicit selected input/output identities, rate and complete native channel order,
and prepares the existing SDK streams. It does not choose a default device or
change volume. Conversion between declared capture/output rates and monitoring
channel mapping use the existing admitted route. Partial/reordered native-device
channel selections require a future explicit routing adapter; no implicit reorder,
downmix or raw-width substitution is performed.

Warmup limits are prepared consistently with the route. Resource ledgers continue
to govern native banks/DSP/writer storage. The controller FIFO has a fixed entry
count; a separate aggregate GUI/control payload-budget admission is still needed
before enabling product input flows. Linux's installed recorder continues to use
its existing PipeWire controller. The new worker is SDK-free and testable on Linux;
a matching native PipeWire factory and installed Linux/Windows panel qualification
remain open. The Windows factory is not made the default desktop recorder here.

## Acceptance

[Qualification packet](../tests/results/M2/2026-10-11-native-monitor-controller/summary.json)
binds 702 source/checkout inputs including UI sources. Three current objects per
Linux profile (controller, native owner and expanded owned fixture) and two private
archives are compiled; unchanged qualified engine/test objects and archives are
read-only and verified against the preceding EQ qualification. Full commands,
logs, configurations and executable/object/archive hashes are retained. No
binaries or audio payloads are uploaded.

The expanded existing native-startup suite passes Release 17.31 s, ASan/UBSan
24.67 s and TSan 28.02 s. Existing native/route/EQ/failure/pressure workflows remain.
New checks use the real controller/processing/disk workers and simulated SDK:

- Finite monitored recording with initial EQ and a +12 dB immediate edit;
  applied frame-zero acknowledgement, original Stop receipt and native completion.
- Independently reopened bit-exact raw RF64, unchanged saved project and inactive
  native state inspection.
- Stale generation, unresolved-result overwrite and future result release refusal;
  exact release and immutable result retention through shutdown.
- Prepared/active cancellation, partial capture activation failure, disk failure,
  missing current clock and missing capture/render native end receipts.
- Active shutdown, all native owners joined, original fault/prefix preserved.
- Factory blocked on the worker while exactly 16 commands fill the FIFO; full
  admission refuses overwrite and urgent Cancel prevents queued activation.
- Foreign project refusal before SDK preparation/pending job creation and
  Unsupported state when no factory exists.

Guarded callback/processing allocate/free/lock violations remain zero. Four
current Windows objects compile warning-free with conversion warnings enabled:
controller, genuine selection factory, owner and expanded fixture. Current
controller native Windows link/runtime and physical endpoints remain unqualified.

## Preceding EQ candidate: actual Windows outcome

[PR148](https://github.com/rhamenator/soundcurrent-daw/pull/148), exact head
`29fcec9383173590216f0ec6f8811a2963484aab`, tested merge
`174da22ce6b882147b764419687aa07ab37709f1`, run `38186095168`, passed its native job.
All 71 original CTests passed, including six owned recordings with EQ and two
actual EQ publication interruption boundaries. Three original 20-take/five-second
standalone diagnostics actually completed. This executes the preceding EQ/wait
implementation, not the new controller. It does not prove the earlier independent
manual audio-flush stall's cause or qualify physical scheduling.

The desktop job failed: 18/19 suites passed; parent-interruption recovery during
processing reported Incomplete instead of Ready. The exact helper instance exited
with code 2, with audio.partial but no completion marker; the parent had been
terminated and the child deliberately suspended around recovery observation.
The worker's code-2 path is its five-second process watchdog. The original failure
packet cannot establish which interval consumed that budget. Do not relax the
watchdog, change the expected outcome or rerun unchanged merely to obtain green.
Retain the original process identities/file facts and add timing evidence around
acknowledgement, suspension, parent death, recovery scan and resume to qualify a
causal correction. The original earlier manual flush-stall investigation remains
separate.

[Original runtime/failure evidence](../tests/results/M2/2026-10-11-native-monitor-eq-windows-runtime/summary.json)
retains all four terminal original job logs, seven native artifact logs, two
desktop artifact logs, API/job/artifact metadata and exact tested-source bindings.
Linux subsequently completed successfully; native, Linux and cross jobs pass while
the desktop job fails. Original artifact
ZIPs, executables and audio remain local. PR148 remains draft; its failed required
desktop gate prevents ordinary admission.

## Next concrete implementation

Diagnose the Windows interruption test's watchdog budget with actual owned-process
timing while preserving its original assertions. Then connect the new worker to a
visible finite recording/device/EQ/result-review panel, localize the controls,
charge GUI/control payloads and qualify current native Windows execution. Add
native PipeWire integration and installed/hardware latency/alignment/recovery
workflows. Reuse this coded owner/controller; no extra placeholder preparation
layer is required. Full-suite mixing/automation/plugins/content/import/profile/
installation/European-language parity remains incomplete.
