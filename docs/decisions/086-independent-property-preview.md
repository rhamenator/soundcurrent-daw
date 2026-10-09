# ADR086: independently validated source-property preview

Status: accepted for read-only inspection; conversion unqualified.
Date: 2026-10-09 UTC.

## Context and options

The owned C++ property model needs an observable desktop workflow without granting
foreign projects media/plugin execution or silently converting source semantics.

1. Run the foreign mapper in the GUI process: less protocol work, but removes the
   existing parser process boundary and lets source details bypass independent
   admission/validation. Rejected.
2. Send rendered strings or destination Session objects: simpler UI binding, but
   loses stable locale-independent IDs, exact provenance and explicit source gain
   layers, and makes conversion claims premature. Rejected.
3. Send versioned numeric/range/object/evidence metadata with exact source bytes
   retained separately; independently validate it before Qt observes it. Selected.

## Decision and consequences

Keep the existing worker and add an explicitly versioned ASCII property protocol.
Validate ownership, full occurrences, source shapes/token slots/numeric domains
and reconstructed line evidence independently. Fixed scratch plus conservative
shared-ledger grants preserve bounds and lifetime. Maintain v1 outline support and
the existing portable container; absence of properties disables that tab.

No new third-party product dependency is selected. Existing C++20/resource/crypto/
nlohmann JSON/Qt infrastructure supplies the workflow; Python and the standalone
C++ probe are test-only. Original changes are GPL-3.0-only; no vendor source,
proprietary asset or equalizer change is included. An independent field validator
duplicates the admitted source-shape contract intentionally: schema evolution must
update both sides and native/corruption witnesses, increasing maintenance cost.
Linux and Windows compilation/runtime/Qt/filesystem/installer gates stay separate.

Read-only tables show source properties and explicit unsupported/missing/unverified
state; no current-project mutation or media resolution follows. Declared payload
admission is not a hard OS sandbox. Semantic mapping and aligned audio comparison
must precede any compatibility promotion. Full frozen-reference scope is unchanged.
