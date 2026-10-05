# ADR-005: Direct format adapters and bounded process isolation

Status: **proposed for M6**. Date: 2026-10-05.

Native Linux VST3/CLAP/LV2 remain requirements. Default hosted plugin DSP runs in supervised children with bounded shared buffers. Discovery has separate timeout-limited children; serialization/editor lifecycle follows each SDK's thread rules. Main engine never waits beyond its declared period for child completion.

Pipeline lookahead/IPC adds explicit latency accounted for by PDC. Monitor mode tradeoffs are visible; trusted in-process hosting, if offered, is an opt-in crash-risk choice. Process separation contains failures and is not automatically a security sandbox. Legacy formats, editors under Wayland and proprietary bridges remain unresolved compatibility gates.
