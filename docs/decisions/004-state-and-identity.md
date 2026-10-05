# ADR-004: Versioned state and persistent identities

Status: **proposed for M1**. Date: 2026-10-05.

Use versioned JSON session snapshots plus project-relative immutable media and separately hashed opaque processor state. SQLite may index content later, but is not the audio-thread state owner. UUID object identities and symbolic parameter IDs survive reorder/rename. A GUI preset application is one semantic undo transaction.

Atomic save, recovery journals and conservative migration copies are mandatory. Existing EQ index-based controls and QSettings files are not adopted as the DAW project format. Whole-session serialized DSP objects on RT are rejected.
