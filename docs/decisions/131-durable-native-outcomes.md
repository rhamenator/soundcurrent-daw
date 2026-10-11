# ADR131: bind original native outcomes to durable raw evidence

Status: accepted for disk-side transaction and portable recovery evidence; native activation gated.

Use a separate versioned auxiliary native journal around original raw capture.
Begin pending state before caller activation. Copy joined bounded SDK results into
disk/control ownership; never serialize, hash, allocate or publish files in SDK/
processing callbacks. Preserve legacy raw format/schema/meaning and keep raw PCM
recovery independent of monitoring outcome and playable/project admission.

Verify both raw and native state under one exclusive inactive job lease. Bind the
original spec/run/descriptors and exact raw journal/sample/stamp hashes/extent/phase.
Report immutable original outcomes after raw finalization, or against a verified
inactive partial common prefix after writer failure. Native delivery, bank
consumption, SDK commitment/slack/drain and monitor ordinals remain distinct.
Never invent an SDK end/HRESULT, successful monitoring, or project clock mapping.
Contradictory Complete claims refuse admission; invalid diagnostic facts remain
original evidence under their explicit failure classification.

Old/absent outcomes cannot become completed monitoring. Pending state survives the
raw/native publication interval; reported missing owners remain incomplete.
Validate bounded schema/types/keys/enum tokens and preserve wide unsigned clocks
and signed HRESULTs. Future backends/migrations need explicit paired admission.

Portable fake-SDK outcomes, composed owners and abrupt process-exit tests qualify
this transaction only. Windows object compilation is not runtime execution;
power-loss, actual SDK startup/deadlines/device loss and installed recovery remain
open. Keep the factory disabled pending startup/capacity/clock confidence and
actual native qualification. [Checkpoint199](../199-durable-native-outcomes.md)
records frozen evidence, preceding Windows execution and next task.
