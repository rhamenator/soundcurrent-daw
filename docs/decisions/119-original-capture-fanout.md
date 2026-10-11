# ADR119: independent original capture banks for raw recording and monitoring

Status: prepared transport implemented and locally qualified; native composition
and platform/workflow qualification remain open. Date: 2026-10-11.

## Decision

Reuse the original GPL stamped capture bank twice under one admission/retirement
lease. Add an off-RT payload-charge facade to the existing bank without changing
its validation or prepared ownership contract. No new library, license, native
driver or persistent project schema is required.

Capture publishes the entire original float32 packet to the raw route first,
then to monitoring. Independent consumer leases prevent disk and DSP from retaining
SDK backing or modifying each other's samples. Two bank copies cost additional
bandwidth and memory; bounded admission is explicit. Monitoring queue loss after
raw acceptance retains the additional raw packet and ends further publication.
Separate acceptance counts and original first-fault metadata expose that policy.
No atomic two-queue guarantee or implicit native retry is provided.

| Alternative | Assessment |
|---|---|
| One queue with two consumers | Violates the existing SPSC ownership contract |
| DSP before the raw queue | Couples raw preservation to conversion/monitor failures |
| Shared slabs with two-reader reference counts | Could reduce copying, but adds retirement/free-list and stalled-reader complexity; defer until measured need |
| Wait for disk completion on capture | Violates the real-time boundary |

Stop/cancel preserve drainable accepted prefixes. Consumer failures observed after
capture finishes are settled by control only after all owners join. First-fault
publication has one writer at a time across this explicit ownership transfer.
Joined destruction retires the group credit after both banks, outside callbacks.

[Checkpoint186](../186-capture-raw-monitor-fanout.md) records three-owner Release,
sanitizer and race evidence and its limits. Next: separate typed monitor output
transport, durable original metadata/raw mapping, then native owner composition.
Full DAW parity, processing quality and installed/platform gates remain open.
