# Select a published track before the next UI tick

Date: 2026-10-06. Scope: desktop selection and snapshot consistency.

## Reproduction and fix

The control worker can finish Open while the GUI timeline still holds no project.
Previously, `selectTrack(id)` checked only that displayed timeline, refusing an
ID already present in the canonical session. A deterministic fixture completes
Open without processing Qt events, verifies an empty displayed list, then tries
to select the second canonical track. It fails at selection on `6d2bf26`.

Selection now polls the published model before resolving the stable ID. Each
timeline/inspector redraw also uses one captured canonical snapshot; it cannot
show an inspector from a later Open completion alongside the earlier timeline.
Ordinary worker snapshots and future ticks remain asynchronous. No busy wait,
blocking worker join or audio operation is added to product selection.

The fixture then checks the displayed track list, selected ID and playback
factory's prepared track. Selection neither edits canonical state nor activates
audio. Reopening/saving content remains unchanged. The first post-fix fixture
incorrectly expected three list rows in its two-track data; that downstream
test-only expectation was corrected after its exact handle terminated. The
original early selection failure is retained with a frozen executable and hashes.

## Evidence and limits

The deterministic fixture passes in Debug and Release. Three affected desktop
groups pass in Debug (7.62 s). A serial full ASan/UBSan/LSan suite passes 29/29
(66.30 s), including punch, timeline, UI, recording/recovery and export.
[Evidence](../tests/results/M2/2026-10-06-selection-before-ui-poll.json) pins the
exact inputs, executables, logs and original observations. Punch executables and
the Windows headless core remain unchanged by this GUI fix; native Windows Qt
execution still requires qualification.

The previous sanitizer suite's combined selection/preparation refusal had no
original branch/admission snapshot. This reproduction proves a related defect,
not that earlier failure's exact cause. All 21 observations remain. No passing
suite erases sustained native timing failures or establishes frozen parity.
All 24 borrowed inputs and 92 frozen contracts match; no equalizer writes,
new dependency, host-policy change, purchase or publication.

Next perform a serial default-recording PipeWire regression for the punch engine
change, then implement latency-aware musical locator preparation and persistent
desktop/native punch workflows. Auto monitoring, loop/take lanes/comping and the
full professional, import, profile, Windows and all-Europe goal remain required.
