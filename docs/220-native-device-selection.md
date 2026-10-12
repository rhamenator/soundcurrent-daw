# Explicit native recording devices

2026-10-12. Full suite scope remains active and incomplete.

The native recording panel now offers capture and monitoring device selectors,
Refresh devices and explicit channel mapping. Choices begin unselected. A refresh
preserves an unchanged explicit device choice, marks a removed input unavailable
and never substitutes another device. Reconnection does not silently choose an
input. Same native channel order and explicit mono-to-stereo duplication are the
currently available maps; a full custom native matrix editor remains open.

Enumeration runs on the existing control worker. The genuine Windows adapter
bounds endpoint text and channel geometry before projection and validates selected
ports against a fresh SDK inventory before preparing streams. Refresh is refused
while an owner or unresolved take exists. The UI reads immutable projections and
performs no device queries or media I/O.

Each retained inventory owns a resource lease until its last snapshot consumer
releases it. The inventory child budget can share the project parent ledger. Quota
refusal preserves the previous projection and revision. GUI models reserve their
own conservative descriptor/text allowance before construction. These allowances
are not exact allocator/RSS accounting; permanent snapshot/result accounting and
other aggregate ownership gates remain open.

StudioWindow prepares a context for the selected track using the canonical project,
layout, input latency and EQ, then revalidates it when choices are accepted. Native
route and raw writer allocations share the project ledger. The Windows main entry
point supplies the native adapter, making this an actual application tab rather
than only an injected widget. Linux retains its existing PipeWire recording path;
its new native stream adapter remains open. Neither default devices nor volume are
changed. Physical-device and installed Windows qualification remain open.

Visible acceptance covers no initial selection, off-GUI enumeration, explicit
width/mapping refusal, fresh rejection of a stale input before stream construction,
disconnection/reconnection choices, a finite selected-device recording and owner
retirement. Direct controller acceptance verifies quota refusal, held projection
credits, replacement and shutdown. Existing panel/main-window and native startup
workflows remain present. All SDK devices are simulated.

The first selector test stalled because the fake SDK awaited a test disk callback
that the application-created context does not supply. The controlled new case now
paces its existing ten-millisecond packets independently of that test-only disk
receipt; original cases retain their previous pacing. Original failure/source/logs
are retained. Fixture counters shared with the worker are atomic. A compiler
warning from conditional temporary endpoint vectors was removed using an explicit
owned vector.

Native Release, ASan/UBSan and TSan pass 19.63/26.84/29.85 seconds. GUI Release
localization/panel/loop-controls/desktop UI passes 16.91 seconds, including 66 panel
checks, and the application entry point links. GUI ASan/UBSan passes all 66 recording-panel checks in 11.15 seconds with a
fresh dependency closure. Prior system Qt/D-Bus TSan reports remain unqualified. Linguist has
34 catalogs, 959 source keys, 3,168 drafts and zero promoted native reviews/UI
qualifications; catalog and 13 refusal audits pass.

Preceding PR154/run38200717967 passes all four jobs: Windows native71, desktop20,
Linux144 and the core cross-build. Original logs and artifact JSON include the
forced 32-frame Stop-delivery receipt. That candidate predates device selection.
Current Windows compilation/runtime, physical/native alignment, usable raw-take
attachment/reopen/export and installed preview gates remain to be qualified.


## Retained qualification

[Current packet](../tests/results/M2/2026-10-12-native-device-selection/summary.json)
binds implementation `665f8cdb923f74503af58db7deea9a3357a5615d`, 784 exact source/
checkout inputs, generated commands, source/object/archive/executable hashes,
original failure and fixture source, native forced-case metadata and full logs.
The GUI ASan/UBSan receipt binds 110 fresh objects and the dependency closure.
Four current controller/SDK/core fixture objects cross-compile warning-free for
Windows; this is not Windows GUI link/runtime or physical-device proof. No binary
or audio payload is published. GUI thread-sanitizer qualification remains open.
Backend diagnostics shown in tooltips are escaped as text.

[Preceding runtime packet](../tests/results/M2/2026-10-12-native-stop-qt-windows-runtime/summary.json)
retains all four original terminal logs, 39 native artifact log/JSON members,
19 desktop artifact log/JSON members and exact PR154 source bindings. It includes
original successful Stop receipt JSON and does not reinterpret historical failures
as causally resolved. ZIPs remain local and are excluded from the public packet.

## Next implementation

Qualify the current Windows GUI/default entry point and actual native selection in
CI, then use a brief owned VM/physical workflow when needed under the user's VM
policy. Automatic idle device notifications, full custom native channel mapping,
permanent snapshot/result ownership, native Linux adapter and native raw take
alignment/attachment/reopen/export remain open. All later frozen-reference,
processing-quality, content, compatibility, import, profile and European-language
requirements remain in the active full goal.
