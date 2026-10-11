# ADR129: retain SDK render commitment and end facts separately

Status: accepted for prepared bookkeeping and compiled SDK hooks; activation gated.

A filled callback buffer is not proof of successful native release, queue drain
or Stop. Add fixed bookkeeping to the existing WASAPI renderer, expose the actual
prepared descriptor, and append optional end/stop-poll callbacks. Preserve original
native lease clocks/results, callback presence/action, successful committed
content, native-only boundary silence, actual padding/empty queue and Stop HRESULT
as separate fields. A failed Stop must not erase prior empty-queue evidence or
turn it into a successful drain. Unstarted exits have no manufactured Stop call.

One SDK owner supplies actual facts; one end callback runs after Stop/exit before
join. External context survives the join. Joined control cannot receive a fake
empty successful end. Bounded methods keep native APIs, allocation, blocking
locks and I/O outside their contract. Duplicate/post-end updates are inert;
inconsistent bounded receipts retain original inputs and an invalid flag.

No dependency, driver or project schema change. Full native content leases may
include terminal slack, so committed lease counts are not monitor/project ends.
Compile and prepared unit evidence do not qualify native runtime, timing or
audible output. Keep the duplex factory disabled pending prepared monitor fill,
render-clock transport, durable original SDK-end recording state, live startup
capacity/confidence and installed SDK failure/drain/retirement qualification.

[Checkpoint197](../197-original-native-render-end.md) records exact frozen source,
retained evidence, preceding independent Windows capture execution and next task.
