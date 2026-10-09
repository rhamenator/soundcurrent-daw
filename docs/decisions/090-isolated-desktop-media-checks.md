# ADR090: explicit media choices and an isolated checker

Status: selected; local Linux qualification, new native Windows gate pending.
Date: 2026-10-09.

| Route | Functionality, license and integration cost | Decision |
|---|---|---|
| Decode media in the GUI process | Existing libsndfile; internal allocations/time and failures affect the desktop | Keep foreign decode in a child |
| Automatically resolve saved foreign paths | Convenient; saved text supplies neither approval nor stable object identity | Require explicit folder or replacement selection |
| Introduce another process framework or audio codec | New licensing, maintenance and packaging burden without a current requirement | Use existing Qt QProcess/threads and libsndfile |
| Adapt the original import-controller lifecycle to the approved WAVE checker | Existing GPL code, Qt 6 and crypto/decoder dependencies; separate exact v2 report contract and sibling executable | Selected |

The report model stays framework-independent. All decode, filesystem, JSON,
allocation, locks and process management run outside audio callbacks. Virtual
rows retain immutable source/object evidence and same-ledger result borrowers.
An explicit native-leaf route supports manually selected nonportable names; it
does not relax foreign-reference rules or allow Windows alternate streams.

No new runtime library, proprietary algorithm/content, external code snapshot or
equalizer change. Original adaptations retain GPL-3.0-only notices. Existing Qt,
libsndfile, nlohmann and crypto notices/source duties apply. QtTest and synthetic
process fixtures are development-only.

Costs: separate worker installation and native execution qualification, strict
schema maintenance, child lifetime/cancellation accounting, broader-format
coverage, and separately qualified OS memory/CPU containment. Declared resource
credits are not an OS sandbox. See checkpoint124 for acceptance and remaining
conversion, installation, localization and full-parity gates.
