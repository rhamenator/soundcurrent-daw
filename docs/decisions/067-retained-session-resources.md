# ADR067: leases for retained immutable Session ownership

Status: implemented checkpoint, 2026-10-07; combined resource admission remains open.

Use a framework-independent shared accounting ledger and move-only RAII leases
for immutable Session blocks. Admission precedes copying; block destruction
returns credit after its last shared borrower releases the payload. Reuse current
immutable blocks for saved revisions, IO saves and barriers. Stage publications
before canonical edits/history transfers; retain the starting gesture publication
so full-budget Cancel needs no new Session allocation.

Keep a trusted configurable snapshot byte policy independent of project data.
Reserve the state maximum before unknown-size Open, then adopt/shrink validated
state. Use existing `ProjectBudget` and history policies for their own scopes;
do not describe their sum as an admitted whole-process envelope.

The C++ standard-library mutex is confined to off-audio owners. CMake links the
existing platform Threads facility to the session target. No third-party library,
project-schema change, RT work or equalizer modification is introduced.

See [ownership, workflows, accounting scope and acceptance](../82-retained-session-resources.md).

Review correction: if attachment verification returns at the captured model
revision, reuse its leased proposal rather than reconstructing a third Session
with new clip IDs. Later accepted edits still require current-state merge and
publication admission. A deterministic full-budget regression guards this path.
