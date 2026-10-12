# Native recording in the main DAW window

2026-10-11. Full suite scope remains active and incomplete.

## Implemented

StudioWindow now embeds the actual NativeMonitorPanel in its recording tabs when
a native stream factory is supplied. Explicit worker-selected preparation context
is validated against the current project, selected track, channel layout, input
latency declaration and initial monitor EQ. A just-published project Open is
synchronized before resolving selection. Context and the inspector EQ copy reserve
GUI payload credit before copying.

The main transport Stop and window close send urgent native cancellation. Normal
project replacement, routing/structural changes, Undo/Redo and conflicting
recording/playback preparation are refused while a native route or retained result
is owned. Parameter edits remain available and published canonical EQ values are
forwarded through stable addresses and revision receipts. Individual accepted
parameter intents advance the local comparison state; a full command FIFO retries
remaining changes. Monitor-only EQ bypass remains separate from saved track EQ.

The close coordinator waits for native recording to become inactive, preserves its
original result on disk, follows the existing save decision, requests all workers
to shut down, and accepts close only after their Closed snapshots. Cancellation of
the main close decision keeps the retained take available. The raw take is not
attached as a time-aligned playable Asset. Reset retires the previous window
context; a subsequent run needs fresh explicit preparation.

Idle context is invalidated after track/project/EQ changes. The constructor remains
compatible with existing callers. Existing manual recording guards retain their
previous behavior; additional history/routing restrictions apply to native ownership.

## Qualified scope

[Local evidence](../tests/results/M2/2026-10-11-native-window-integration/summary.json)
binds implementation `545be56fb8056711990b39c029540147edef04ab`, 784 exact source/
checkout inputs, generated build commands, 108 fresh object hashes and archive/
executable hashes. A fresh Release dependency closure was built with one low
priority job. Logs retain diagnostics in unchanged mix/playback and older test
sources; this is not an all-warning-free build claim.

Release passes three suites in 13.13 seconds: actual native recording panel
(6.66 seconds), existing loop controls (0.30 seconds) and broader desktop UI
(6.17 seconds). The expanded panel fixture has 41 checks. It exercises the real
main window tab, worker preparation, explicit context refusal, conflicting
transport/project guards, canonical gain edits and applied monitor receipts,
saved track settings, no raw attachment, retained results and close during active
recording. SDK streams are simulated; recording, processing, control and disk
workers are real. Original standalone panel workflows remain present.

Original compiler failures are retained: a custom widget lookup required Qt
metaobject support and now uses the named base widget plus checked dynamic cast;
Qt keyword macros conflicted with engine fields and engine declarations now
precede Qt in the main window header. A failed initial selection observation led
to synchronizing published Open/selection before validating context. An attempted
CTest observation without a built executable is explicitly retained as Not Run.

Linguist audits and 13 catalog refusal cases pass: 34 catalogs, 947 source keys,
3,135 draft translations, zero promoted native reviews or UI qualifications.
New strings remain unfinished in non-English catalogs.

Current ASan/UBSan, TSan and Windows execution of the **main-window integration**
remain open. The earlier system Qt/D-Bus TSan reports remain unqualified. Permanent
inventory/snapshot/result ownership, native route aggregate memory preferences,
locale-specific numeric readouts and installed/device/hardware workflows also
remain open. The new factory is still opt-in: production device enumeration,
explicit dropdown selection and the default main executable are not wired here.
Linux's installed recording workflow remains available through its existing backend.

## Preceding panel candidate: Windows results and failure

[PR152](https://github.com/rhamenator/soundcurrent-daw/pull/152), head
`c6dc639ebe6c2e710ba9780e2aad36998b9aa166`, tested merge
`639fa5c3c46a78f070b888025a275a1fc7e7dbb2`, run `38195499335` is terminal **failed**.
Windows desktop passes all 20 tests, including 23 actual panel checks. Linux passes
144 tests and the Windows core cross-build passes. These predate this integration.

The native job passes 70 of 71 suites. Native-monitor-startup fails after a finite
owned recording exceeds its original five-second completion wait. After explicit
cancellation it reports capture status 0, render status 1, no processing fault and
96,480 retained raw frames. This post-cancel report does not establish which owner
was stalled before cancellation. Later controller checks in that suite were not
reached. The other manual suite and three original standalone diagnostics complete.

[Original runtime evidence](../tests/results/M2/2026-10-11-native-panel-windows-runtime/summary.json)
retains all four terminal logs, seven original native artifact log members,
19 desktop artifact log/JSON members, artifact metadata and exact 1,107 changed
blob/784 input bindings. No binaries, audio or ZIP payloads are published. The
failure is not treated as success; no timeout increase or unchanged retry is used.
Earlier independent failures remain unresolved in their original records.

## Next implementation

Capture bounded original owner/processing/Stop state **before** cancelling a
failed finite native recording, and retain its owned metadata in CI. Use that
evidence to correct the stalled completion path without changing its acceptance
limit. Qualify this integration with sanitizers and Windows execution. Then add
worker-owned device enumeration and explicit dropdowns, bind aggregate retained
memory and enable the complete user workflow. Native Linux integration, alignment,
reopen/export, all later milestones, imports, profiles and all-European delivery
remain part of the full goal.
