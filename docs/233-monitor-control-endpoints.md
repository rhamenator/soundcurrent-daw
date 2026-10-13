# Shared prepared recording control endpoints

Checkpoint233 adds a framework-independent worker endpoint protocol and owned
WASAPI/PipeWire implementations. The existing native controller now dispatches
its WASAPI workflow through this seam. PipeWire control snapshot/Qt result wiring
and verified raw-take placement/export remain next; this is not UI parity or M2
completion.

Declaration and outcome variants retain NativeOriginalRecordingSpec/Result and
PipeWireRecordingSpec/Result as distinct alternatives. Common progress contains
counts and owner terminal flags only; no graph/POSIX-to-QPC conversion, negotiated
hardware inference or audible-drain/alignment claim. Monitor ordinal first and
actual native evidence remain independent. Factory construction/connection,
activation, service/readiness, Stop/cancel, EQ event/ack and joined results run on
one serialized control worker; no GUI/audio callback calls this protocol.

Endpoint object leases charge fixed facade storage through the route ledger and
retire after the owned route/native callbacks/processing/disk owners. The controller
retains its bounded16command queue, urgent cancel epoch, immutable snapshots,
stale-generation/project identity checks and explicit result resolution. WASAPI
saved-state/admission/export types remain unchanged. PipeWire factory connects
explicit stable selected ports while inactive and never changes default devices.

Implementation6ca0e08 binds831 selected source/checkout inputs. Fresh129step
Release build plus18step acceptance/native build; updated PipeWire endpoint suite
and unchanged native startup/controller suite pass2/21.95s. The latter exercises
finite monitoring, immediate EQ acknowledgements, cancellation, partial activation,
disk/clock/end loss, stale commands, resource admission, saved result/reopen and
shutdown via the refactored worker. Simulated SDK evidence does not qualify Windows
hardware or Qt presentation. Changed targets compile without new warnings; older
mixer warnings remain retained.

The genuine private PipeWire1.6.2 fixture now creates/selects the shared endpoint
and consumes its typed PipeWire result. It preserves99,072 raw frames, delivers
8,192 monitor/sink frames, retains387 capture/408 render receipts and reopens exact
durable state. Callback allocate/free/lock audit hits0. Source/sink and daemon join;
no physical/default/volume/playback/VM/equalizer changes.

Four current Windows endpoint/controller/test/fixture objects compile warning-free. Current sanitizer/native Windows runtime
qualification remains pending; previous completion-state sanitizers are not current
endpoint qualification.

OriginalPR166run38322167759 exacta2937ac allfourjobsSUCCESS: Linux153/263.15s,
native80/165.75s,desktop20/71.88s,crosspass. Originallogs/selectedartifacts and
all829qualified testedmergeblobs are retained; ZIP/audio staylocal. This qualifies
prior completion/clock code on independent Windows tests, not current endpoint
runtime or physical audio. Earlier local clock/PR164owner/PR163pool causes remain
unresolved; no unchanged rerun replaces their failures.

Next: extend controller preparation/result/saved inspection with typed PipeWire
alternatives; share common UI status/controls, retain backend-specific original
facts and verified evidence lists. Explicit raw admission/user placement/export,
project barriers, resource/cancel/close/Undo/Save and both native platforms need
acceptance. Full frozen-reference/all-Europe scope remains active.

Evidence: [bound qualification summary](../tests/results/M2/2026-10-12-monitor-control-endpoints/summary.json).
