# ADR 017: saved monitoring preference and accepted-prefix preparation

Date: 2026-10-06. Status: accepted for S8b.

## Decision

Store Off/Post-EQ recording monitoring per track in the Qt-free model. Emit
schema 1.2 stable strings, explicitly migrate 1.0/1.1 to Off and reject unknown
versions/modes. Add typed stable-track-ID edits to shared semantic Undo/Redo.
Prepare from an accepted immutable controller-barrier prefix so pending mode/EQ
edits cannot be missed by immediately clicking Prepare.

## Consequences

Reopening restores the preference passively. A prepared graph retains its captured
mode until Stop/preparation; Undo updates state and displays any mismatch. Raw
capture and default export remain independent of monitoring preferences. An old
build rejects the newer schema; it must not strip the field. This is a control-side
addition, with no allocations, locks or I/O added to real-time processing.

Auto/overdub/multitrack/control-room monitoring and equipment routing remain
separate required work. See [contract and acceptance](../27-monitoring-preferences.md).
