# Portable recording error details

Useful-preview checkpoint, 2026-10-08 UTC. This extends the single-track
[first-fault receipt](91-recording-fault-diagnostics.md). It does not change the
recorded samples, project schema or recording checkpoint format.

## Workflow

After native callbacks and the recording writer have joined, the single-track
PipeWire owner attempts to save its original first fault beside the raw take.
Reopening the project scans inactive recording jobs and offers **Review
recordings…** when saved error details exist, including for already attached
takes. The review dialog displays the same contextual clock/reason explanation
as the live notice. Informational error rows cannot start an audio preview,
attach a take, dirty the project or activate a recording endpoint.

A missing sidecar is normal for older takes and successful recordings. Invalid
optional metadata produces a separate warning; it cannot hide or invalidate a
verified raw checkpoint. If writing the sidecar fails, the live notice reports
that separately. The original writer/take-verification error keeps priority and
the audio recovery workflow remains available.

## Storage contract

`media/capture-ASSET-ID/first-fault.json` uses format
`soundcurrent-recording-fault`, schema **1.0**. The envelope binds the project,
track and asset IDs, sample rate, channel layout and start frame to the job's
existing journal and canonical directory. Numeric enum IDs now have explicit
values, preserving their previous numbers. No localized text or absolute project
path is stored; moving the whole project preserves the diagnostic identity.

The fault retains the rejected and previous accepted clocks, engine/captured
frame observations, proven/prepared buffer bounds, channel observations,
generation and processor/capture statuses. Absent clocks are JSON null. Invalid
observed input rates or quanta are valid diagnostic observations; previous
accepted clocks must satisfy the prepared contract. A control notification can
observe a captured count before an in-flight callback finishes. Its original
observation is retained even when the final committed take is longer. This is
not a recording-alignment correction or permission to rewrite the receipt.

Parsing is bounded to **16 KiB**, depth four, exact current fields and typed
integer/boolean ranges. Duplicate keys, unknown schema versions and identity
mismatches are refused. Reads use the existing shared cooperative job lease;
publication requires an exclusive, already existing inactive-job lease. Missing
legacy locks cannot be created as a side effect of diagnostics. A unique owned
temporary file is flushed and atomically published without replacing an existing
destination, followed by directory synchronization. Repeating the same receipt
is idempotent; a different receipt is refused. Existing media/journal bytes are
not rewritten. Plain-path and descriptor checks reject linked metadata using the
existing media I/O implementation; this is not a filesystem sandbox claim.

All parsing, allocation, serialization and disk operations run outside audio
callbacks. A storage exception is retained independently as `exception_ptr` and
formatted later on the worker/control side, outside the owner's noexcept stop.
Filesystem failure can prevent durable metadata; the raw recovery evidence and
current in-memory first fault remain independent.

## Acceptance and limits

Acceptance covers exact round trips, immutable/idempotent publication, active and
reader-held lease refusal, attachment/reopen, Unicode relocation, empty failed
jobs, delayed control observations and malformed optional metadata. Corruption
cases exercise identity, schema, duplicate keys, nesting, size, numeric types,
clock fields and symlinks while comparing original audio/journal bytes. The UI
checks reopen both attached and unattached takes, with no endpoint construction,
project mutation or audio-preview action from an error row. Writer and metadata
failures remain separately observable. Private native checks exercise both
successful publication and a refused destination through the production owner.

Detailed writer exception text, multi-track/manual-punch sidecars and native
Windows file sharing/runtime remain separate unfinished workflows. Four new Qt
messages bring the catalog to **543** keys, with **538** finished English entries
and five unfinished numerus entries. The 32 non-English drafts still translate
eight entries each; no language gains review or complete UI qualification.

Latest Linux Debug passes **63/63, 179.68 s**, and the seven affected
ASan/UBSan/LSan checks pass **7/7, 31.02 s**, with matching source/resource
inputs. Two production-owner checks on a private PipeWire 1.6.2 server pass,
preserving verified **1,024-frame** prefixes with successful and deliberately
blocked metadata publication. Both reproduce clock29 cycle1→2 position0→0
instead of1024. They retire owned nodes/links and preserve host default/link
fingerprints. Two changed storage translation units compile for Windows; this
does not qualify Windows locking, GUI, audio or installation.

The [receipt and evidence capsule](../tests/results/X007/2026-10-08-portable-recording-faults.json)
retain scoped inputs, test logs, exact latest native launcher/subcommands,
original compile failures, take/sidecar bytes and redacted route comparison
inputs. All **2,912** logical entries and ZIP CRC are verified; the capsule is
**65,419,989 bytes**. Initial native/cross-compile cohorts remain distinct from
the latest control-observation correction and explicit enum IDs.

The currently prepared DEB at commit `97a307fcf2ba` predates this feature. Code
and private Debug checks do not establish an updated installed preview. Next:
qualify a new package/source pair and reopened installed-GUI diagnostic workflow,
then investigate native acquisition/startup alignment without discarding valid
silence. The original native71 observations and 2,048-frame leading silence
remain unresolved. Full desktop/physical/real-time and native Windows previews,
X004/X005/X006 and all frozen F/Q/C/N parity contracts remain incomplete.
