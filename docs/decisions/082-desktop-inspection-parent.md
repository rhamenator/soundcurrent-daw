# ADR 082: admit and validate foreign inspection in a separate desktop controller

Status: accepted for the bounded structural preview, 2026-10-08.

Use a low-priority Qt control thread to own one QProcess and an immutable result
with shared application memory leases. The framework-independent source/report
libraries retain original bytes and enforce protocol bounds. The real foreign
grammar remains in the separate worker introduced by ADR 081/checkpoint 114.

This reuses Qt already selected for desktop lifecycle and Unicode paths. It adds
no process/runtime dependency. A direct parser on the GUI thread would block
interaction during file reads and malformed inputs. A process boundary alone
does not prove provenance, safe retirement or memory admission, so the parent
independently captures/hashes source, validates the complete report and owns the
actual process through termination. The GUI displays a read-only structural
preview; it does not mutate canonical project state.

Alternatives retained for later evaluation: an OS-restricted child launcher for
hard memory/access limits and bounded brokered source delivery for storage
deadlines. Qt's internal buffers and provider/runtime overhead are not claimed
as precisely ledger-accounted RSS. No plugin execution, source dependency
resolution or semantic native-format qualification is included in this decision.

Consequences: source and rows can outlive the controller safely; one helper must
ship beside the main application; new packages require independent tested helper
identity. Windows Qt/native installed qualification is a separate gate. Persistent
opaque-state delivery, approved conversion and full X004 adapters remain required.
