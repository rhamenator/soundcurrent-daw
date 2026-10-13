# Typed PipeWire recording controller and panel

Checkpoint234 connects PipeWire preparation, live results, inactive inspection and
original WAV export to the shared worker controller and Qt recording panel.
Linux main selects the genuine PipeWire factory when built with its SDK; Windows
continues to select WASAPI. Frozen full-suite/Windows/all-Europe parity remains active.

## Control and saved state

An explicit optional PipeWire preparation carries expected graph geometry, EQ/map,
selected ports and root. Worker preflight validates the saved project/track/layout/
asset and generation. Declared geometry, live result, reopened state and export
receipt remain typed separately from WASAPI/QPC objects. Common progress contains
counts/terminal flags only. Urgent cancellation, bounded16commands, stale-generation
checks, result sequence release and asynchronous close remain in the existing worker.
New PipeWire saved/result ownership charges bounded payloads and remains alive while
snapshots hold references. Inactive inspection requires an explicit project-owned
original-UUID job under media, matching project/asset identities and existing state/
raw/native-receipt leases. No SDK reactivation or clock conversion occurs in inspection.

Original WAV export verifies the exact declaration, finalized wholly finite raw
prefix, full WAV geometry/container hash and source stability; copies bytes through
64KiB pages; flushes and publishes without overwriting. Monitoring EQ is not printed.
PipeWire disposition stays a PipeWire type. Directory-flush warnings retain the
published receipt; cancellation before publication does not leave a claimed export.

Worker inventory uses an inactive temporary PipeWire filter, sorts discovered graph
ports and revalidates chosen stable identities on connection. Reported group widths
are inventory counts, not negotiated hardware formats. UI labels omit unknown graph
rates. Project-rate declarations are checked by actual callbacks; the app does not
force default output, device volume, driver rate or quantum. Refresh/prepare/record/
inspect/export/reopen/release/close flow through worker calls; GUI does not perform
SDK/IO/joins.

## Remaining workflow gates

**PipeWire timeline admission is not implemented yet.** The panel disables its Add
control for that backend; the worker refuses the existing WASAPI-specific admission
path. Proper project evidence must retain all PipeWire manifests, immutable intent/
outcome and paired receipts; it must not be disguised as WASAPI evidence. Next extend
versioned project evidence, approved-file pins/hashes, explicit user placement,
Undo/Save/reopen/playback/export and cross-platform saved-take selection. Original
WAV export and raw take retention do not substitute for this required workflow.
Current installed/native Windows endpoint/hardware, hotplug, alignment/audible drain,
quality, later milestones and reviewed localization remain open.

## Current qualification

Implementation7aadee9 binds836 source/checkout inputs. Five Release suites pass30.61s:
PipeWire endpoint/600ms delayed-activation regression, new typed worker workflow,
unchanged native startup/controller, catalog structure and13negative catalog cases.
Qt native panel suite passes13.16s, including simulated PipeWire Prepare/Record/
Inspect/Export/Reopen/Close and unchanged WASAPI panel/main-window cases. These Qt
cases do not qualify physical devices or native Windows runtime.

The genuine private PipeWire1.6.2 fixture now dispatches through the controller
worker. It preserves99,072 raw frames, delivers8,192 monitor/sink frames, retains
387capture/408render receipts and reopens durable state; callback alloc/free/lock
hits0. Source/sink/daemon owners join; no physical/default/volume/user-playback/VM
or equalizer changes. Five current Windows workflow/controller/test/fixture objects
compile warning-free; current sanitizer/native Windows runtime remain pending.
Initial missing owner declaration include, missing factory initializer and delayed
fixture indentation warning are retained/corrected; existing mixer/playback warnings
remain visible. No test limits or queue/interval/deadline gates were relaxed.

Linguist source inventory/catalogs were refreshed after UI edits. Structural and
corruption checks pass:34catalogs/985keys, zero native-reviewed and zero fullyUI-
qualified languages. Draft catalogs are not completed European language coverage.

## Original PR167 failures and fixture scheduling correction

Originalrun38324363198 exactad5ae0b: desktop20/75.58s and cross pass; Linux151/153
in382.85s fails both catalog audits because the UI source-hash inventory was stale.
Current real Linguist refresh fixes that structural mismatch without weakening audit.
Native79/80 in334.94s fails simulated PipeWire ownership0.99s: raw3840, no monitor
output, raw-side QueueFull14, processing Clock0; capture clocks8accepted/0delivered/
8discarded and render49/0/49. Actual failed project8eab6ab2... was successfully
retained in the original archive this time, including raw WAV/packet/native receipts
and durable outcome. Outcome says disk/storage failuresfalse. Public evidence keeps
metadata/hashes; actual WAV/bin/ZIP bytes stay local. Earlier evidence gaps remain.

The old simulator anchored scheduled callbacks to construction before durable file
preparation. When activation was late, sleep_until deadlines were already overdue,
allowing a catch-up burst before disk/processing scheduling. The fixture now preserves
its independent monotonic wall clock and starts driver ticks at activation; it does
not reset current time or seed clock fits. A600ms pre-activation regression verifies
that first capture timestamps do not replay construction time, with identical slots,
10ms cadence and deadlines. This corrects the fixture design hazard; the original
runner's OS scheduling/preparation timing was not measured, so do not claim its exact
cause is proven. Earlier real pool/timing and Windows failures remain unresolved.

Evidence: [bound qualification summary](../tests/results/M2/2026-10-12-pipewire-monitor-controller/summary.json).
