# ADR065: snapshot-backed lists and viewport interval queries

Status: accepted checkpoint, 2026-10-07; full X006 qualification remains open.

Use existing Qt6 model/view and a custom viewport within the DAW GUI. Borrow
immutable sessions; resolve stable IDs rather than retaining a QObject, widget
or scene item per project row/clip. Build sorted interval indices off the audio
thread and query visible rows/windows. Retain canonical stacking and selection,
checkable recording arms, exact frames, Undo/Redo and Save/reopen semantics.

No new library or engine integration is needed. GUI indices remain linear in
inventory, snapshot changes still rebuild them, and Qt signed-int row positions
need future paging. Explicit refusal is preferable to silent state loss; it does
not change the product's arbitrary finite resource-admitted track requirement.
Combined memory/admission, accessibility and native Windows qualification remain
separate gates. See [implementation and scoped evidence](../80-virtualized-session-views.md).
