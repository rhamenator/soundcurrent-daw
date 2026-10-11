# ADR127: stop capture through its SDK owner and retain its original end

Status: accepted for prepared components and compiled SDK hooks; activation gated.

Finite monitoring must not leave capture running indefinitely while processing
waits for input EOF. A Stop boundary also cannot be inferred from raw counts or
represented as successful when the SDK owner fails or disappears.

Use one generation-bound immutable processing request and one original SDK end
cell. Match prepared geometry and admit conservative ring space for the queue
threshold, every bank slot and one in-flight native packet. Require polling
before each SDK lease. Signal Horizon when generation reaches the planned end,
before downstream output pressure; signal faults/cancellation without SDK calls
on the processing owner. Keep missing owner, invalid receipt, native failure and
healthy results distinct and preserve first causes separately from Stop results.

The native capture owner records actual Start/Stop/callback facts and reports
once after owner exit, before join. Preserve absent Stop for unstarted exits,
full native positions/QPC and actual HRESULTs. Joined reads must not manufacture
empty successful receipts. Publish this original end before capture EOF.

No new dependency, driver or schema is introduced. OS event signaling/joining
remain serialized control operations outside the prepared RT contract. Polling
alone provides no wall-clock deadline and bank bounds provide no throughput
guarantee. Keep the native duplex factory disabled until the actual context,
render finite drain, durable recording binding and platform/installed gates pass.

[Checkpoint195](../195-native-capture-stop.md) records the final frozen source,
Release/sanitizer/RT audits, fake-SDK four-owner composition, SDK cross-build and
remaining native gates. Component tests do not establish full workflow parity.
