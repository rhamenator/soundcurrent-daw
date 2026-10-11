# ADR124: qualify the combined owners under an explicit logical schedule

Status: combined portable integration fixture implemented; native workflow open.
Date: 2026-10-11.

Combine the actual disk writer and original clock transport with the existing
monitor processor before adding SDK activation. Use four actual owner threads and
an explicit phased logical schedule. Guard prepared methods independently; keep
barriers, disk and assertions outside the real-time audit. No new dependency,
license or device driver is needed.

| Alternative | Assessment |
|---|---|
| Substitute a raw reader for disk ownership | Does not exercise durable audio/metadata publication |
| Keep processing control time fixed | Does not test receipt freshness across an advancing run |
| Infer monitor end from original raw frame count | Conflates capture and monitor timing domains |
| Count fixture barriers as native concurrency evidence | Hides the intentionally staged schedule |
| Accept a monitor end behind already-generated PCM | Cannot undo published output or restore its time boundary |

Retain original bit-verifiable raw evidence even when monitoring fails. Require
actual output priming, stable pending pressure and explicit end with real
postroll. Record the late-end refusal honestly; advance finite-end planning is
a production policy still needed before native Stop/punch/loop activation.
[Checkpoint192](../192-combined-monitor-recording.md) records exact scope and proof.
