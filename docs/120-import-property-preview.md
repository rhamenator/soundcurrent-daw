# X004: source-property inspection preview

Date: 2026-10-09 UTC. Frozen product baseline unchanged.

The File-menu REAPER inspection now displays original property values in a
**Properties** tab and retains the complete outline in **Original source**.
Separate track/item/take gains, Unicode names, original units, absent values,
ambiguous takes and unsupported processing are visible. This remains a read-only
inspection: no media is resolved, no destination project is created, and retained
values do not establish equivalent audio behavior. Existing installers are unchanged.

## Worker and protocol

The existing isolated executable accepts `--rpp-properties` and emits
`sc-import-inspection-v2`, adapter `rpp-source-properties-v1`. The previous `--rpp`
outline/v1 mode remains available. Root source hash, actual worker PID, exact byte
ranges and complete outline remain mandatory. Native compatibility stays
`unqualified` and semantic conversion stays `unverified`.

Property schema1 carries object ordinals/parents/kinds, source-type byte ranges,
single-take evidence, stable field IDs1–20, value kinds, original finite numbers or
byte ranges, evidence status/reason and one evidence pair per source line. Enum
values are now explicitly pinned. JSON contains ASCII metadata and numbers only;
foreign names, paths, opaque state and program text remain in the unique original
source bank. The fixed2048-byte emitter sizes the complete report before writing
stdout, refuses a report quota without a partial result, and retains source owners
until output completes. Existing v1 error envelopes and controller deadlines apply.

## Independent parent validation and ownership

The parent continues to admit source, encoded bytes, JSON parsing and row banks
before loading/decoding. It validates complete line coverage, original lexical
keys, tree/extent ranges, SHA-256, PID, protocol and exact key sets. The property
validator separately checks the direct root/track/item/source hierarchy, complete
object/required-field inventories, every mapped occurrence, ownership, source
kind and single-take evidence. Converted status is rejected.

A fixed32-token validator checks each source field's full line shape, exact token
slot, literal quote boundaries, numeric domain and status/reason. It does not call
the foreign property mapper. Values cannot be swapped between gain and pan simply
because both tokens occur on the same line. Missing values have no defaults;
repeated fields have no selected occurrence. Line evidence is independently
reconstructed, including malformed/duplicate/ambiguous precedence and opaque
unsupported state. Unknown source encodings remain exact bytes; UTF-8 display is
not a normalization or media approval.

Conservative row admission includes object/property/line banks and temporary
ownership/bitset/evidence scratch. Actual capacities are checked; scratch retires
before credit shrinks to retained banks. The immutable move-only report owns
source/protocol/row leases, and Qt models share its lifetime. Cancellation occurs
between bounded records. JSON and allocator accounting is declared payload
accounting, not exact RSS or a hard operating-system sandbox. Other imports and
larger trusted envelopes require their own matching bounds.

## Desktop and persistence

The property table is virtual/read-only, with localized property/unit/status
labels, locale number formatting and at most256 original bytes per displayed
value. Complete originals remain saved. Header sizing examines at most64 rows;
there are no per-property widget allocations. Foreign display strings use plain
text; byte-derived tooltips use a controlled escaped wrapper. Tests retain an
HTML-shaped name literally, without active markup.

Save/Open uses the existing admitted disk worker and `SCIBND01` container,
including source and exact protocol bytes. New property inspections reopen after
the original file is removed, with no new child or media resolution. Older v1
outlines still open/save/reopen; their Properties tab is disabled because they do
not contain property evidence. No conversion or native-project migration is
invented. Close/cancel retire models and workers before returning resources, and
the current session, Undo history and original source remain unchanged.

## Acceptance evidence

Local Release passes seven selected tests:73 controller checks,74 Qt preview
checks,259 localization checks,91 bundle checks,140 worker checks,6,013 frozen
native-corpus outline checks and569 property-boundary checks. Six import-related
ASan/UBSan tests pass with leak detection disabled; this is not leak qualification.
The new boundary test runs real children on seven original REAPER7.82/Linux
save/reopen witnesses and uses a separate C++ parent decoder. It also accepts seven
truthful malformed/ambiguous synthetic cases and rejects28 corrupted reports with
valid recomputed checksums, including same-line token swaps and false evidence.
The original failing swap regression is retained before the correction. A later
test-only follow-up reads native manifest/witness text explicitly as UTF-8 on
every host; its separate Release/sanitizer receipts each retain569 checks without
changing the product binaries or frozen source corpus.

MinGW builds the worker, parent/probe, bundle and property targets without running
Windows binaries. The first hosted native run at `1bb2afd` passes14 of15 selected tests but fails
the new protocol test when Python3.14.7 reads the UTF-8 witness using cp1252.
Its exact archive/metadata/log are retained. Explicit UTF-8 fixes the test reader;
corrected native execution and Windows Qt/installed workflow remain separate gates. The predecessor PR62 repaired exact
head `5704601568eec0d80b60b68d40ad621f74387050` passes77 hosted Linux tests and14
selected MSVC tests, including890 original property checks; its exact artifact
metadata, archive digests and logs are retained separately. That predecessor run
does not qualify this new protocol or GUI revision.

Catalog extraction/checking records34 catalogs,669 source keys and3,135 existing
draft translations. New entries remain unfinished outside English; no language
receives native review or full UI qualification from these checks. All-Europe
coverage remains required.

Evidence: `tests/results/X004/2026-10-09-property-preview`; decision:
[ADR086](decisions/086-independent-property-preview.md). No local VM, user audio
route, equalizer checkout, installer or public product binary release changed.
Functional, processing-quality, bundled-content and native-project parity remain
active/incomplete.

## Next concrete implementation task

Add explicit approved media roots and missing-media choices, followed by an
opt-in new-project conversion with Undo/save/reopen and independently aligned
source/destination renders. Establish track/frame timing, gain/pan/fade conventions,
rate/pitch handling and losses before conversion. Multiple takes, MIDI, tempo,
routing, automation and opaque plugin/device/container state require further
workflows. Every registered native/exchange family, including frozen Bitwig and
Cubase references, remains in scope.
