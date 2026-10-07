# Model-backed session lists and viewport timeline

X006 checkpoint, 2026-10-07. Total tracks remain a resource-admitted product
requirement. This checkpoint replaces retained item-per-track lists and
item-per-clip scenes with snapshot-backed list models and viewport painting.
It does not establish full X006, native throughput or frozen-reference parity.

## Implementation and ownership

SessionListModel borrows the immutable canonical session on the GUI thread. It
retains source ordinals and a stable-ID index, and supplies text, check state and
telemetry decoration when the view asks. Tracks, compatible destination tracks,
compatible assets, selected-track clips and recording arms use this model.
QListView uses uniform row sizes and batches layout in groups of128. Comboboxes
use a fixed minimum text-width policy instead of measuring every item for width.
There is no widget, QObject or retained display string per project track/clip.
Recording telemetry decorations update changed arm IDs rather than reconstructing
all project labels on every poll. Prepared/recording arm edits remain disabled.

TimelineView uses one QAbstractScrollArea viewport. On a changed immutable
snapshot, it builds track-ID lookup and sorted per-track interval indices with
subtree maximum ends. Paint queries only viewport rows and their horizontal
frame windows. Mouse hits use the same intervals; canonical clip ordering retains
stacking/hit precedence. Stable-ID keyboard track and clip selectors remain.
Zoom, selection and passive polling reuse the index. Scrolling changes no media,
canonical project values or history. Two playback route/status poll paths now
borrow the selected track instead of cloning a session. The existing inspector
projection cache was already present; its underlying projection remains.

All indexing, label conversion, painting, allocation and retirement occur on the
GUI thread. The framework-independent engine and callbacks are unchanged. No
new dependency or project schema change is introduced (schema1.7). Qt remains
>=6.4; current official APIs were consulted without upgrading the dependency:
[QAbstractListModel](https://doc.qt.io/qt-6/qabstractlistmodel.html),
[QListView](https://doc.qt.io/qt-6/qlistview.html), and
[QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html).
The measured implementation, rather than those API names, determines this scope.

## Resource and accessibility gaps

This bounds visible paint/query work, not every GUI operation or process RSS.
Source ordinals, ID maps and interval trees still grow with project inventory;
a canonical snapshot change rebuilds indices with old/new temporary overlap.
History, inspector projections, snapshot staging, Qt internal geometry and GUI
indices need aggregate measured admission. Selected-track lookup in remaining
adapters can still scan project tracks. Dense overlapping visible clips all paint;
no silent clip skipping or constant work promise is introduced.

Qt list/scroll positions use signed-int rows. The implementation explicitly
refuses an unrepresentable inventory rather than wrapping or truncating it.
Paging beyond INT_MAX remains required by the finite resource-admitted product
target; INT_MAX is an implementation gap, not a product/license track ceiling.

Qt model-backed selectors retain keyboard/accessibility labels. Custom virtual
accessible children for painted clips, screen-reader qualification, DPI/bidi,
reviewed translations and independent native Windows GUI qualification remain.
New strings use translation contexts; no empty catalog becomes a supported
language. No native audio run or VM is part of this checkpoint.

## Bounded acceptance and original observations

The actual offscreen Linux Qt fixture creates8192 mono audio tracks with owned
shared WAV media. It navigates to the last stable ID by keyboard, scrolls and
mouse-selects its clip, checks exact displayed source frames and captured RGBA
pixels, checks20 repaint positions, and verifies unchanged media/project bytes.
A high-ordinal Unicode rename, Undo/Redo and Save/reopen retain exact state.
A separate real project has10000 sparse clips on one track and owned media;
its horizontal interval query and last-clip selector retain exact frame values.
These are regression workloads, not supported capacity promises.

The original paint test compared QColor HSV storage to captured RGB storage;
both reported the same displayed RGB. Its source/executable hashes, logs and
project are retained. The correction compares exact RGBA values. A diagnostic
run retains the original viewport PNG and reports the equal RGB values.
A separate desktop recording-start test once timed out at line458 with phase
Ready/captured0. It passed a subsequent rerun; the original project/media and
exact source/executable hashes/log are retained. The timeout recurred in a second full run, retaining its original project/media.
A deterministic negative workflow reproduces Record enabled with missing input
routes. The GUI now reconciles asynchronous route widgets before readiness and
gates Record/button action and direct Start on all required input/monitor port
selections. Negative missing-input/output checks and actual capture/adoption are
included. Route state at the two original timed-out clicks was not captured;
this verifies a concrete readiness correction without proving those exact causes.
The first full run also failed the older split-clip mouse fixture: it observed
worker publication and dispatched a hit before the viewport represented the new
clip. The fixture now awaits that exact clip rectangle before clicking. Original
source/executable hashes, log and project/media remain retained; this fixes the
test synchronization and does not alter production polling semantics.

The initial sanitizer run passes five affected groups but exceeds the generic
10-second Open wait for8192 tracks. Its original source/executable hashes, log
and project/media are retained; the original controller error state was not
captured. The large-load fixture now measures Open and uses an explicit60-second
allowance (alongside the existing60-second large Save allowance). This verifies
correctness under instrumentation, not interactive load responsiveness or the
original10-second threshold. Final qualification and evidence are recorded in the
[dated receipt](../tests/results/M2/2026-10-07-virtualized-session-views.json).
Final full Debug46/46 takes109.03s; the six affected
ASan/UBSan/LSan groups take202.54s. The8192-track
Open measurement is4133ms in Debug and
24032ms under instrumentation. Its recorded viewport paints2rows/2clips;
the10000-clip project paints304 matches with631 interval-node visits. These figures
apply to this owned fixture/machine and do not establish supported capacities.
Native observations remain71, including the unresolved original sanitizer clock
gap/source CPU cause. All92 frozen F/Q/C/N contracts remain unpromoted.

Next measure/admit combined GUI, state/history and old/new graph envelopes and
expose trusted desktop resource controls. Then continue large recording/adoption,
meters/waveform detail, freeze/bounce, scheduling and sustained Linux/Windows
profiles, alongside X004 imports, X005 equipment, Europe and all remaining goals.
