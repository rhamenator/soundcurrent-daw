# Checkpoint service sanitizer qualification

Checkpoint 241 qualifies the unchanged raw-slot service revision 98b1593.
Full professional-suite parity, Windows, quality/content/native compatibility and
reviewed European localization remain active and incomplete.

## ASan and UBSan

Fresh headless Debug build (149 steps) covers the deferred paired checkpoint owner,
full raw checkpoint hook, journal recovery and canonical native playback fixture.
839 selected source/checkout inputs are bound. Journal recovery/interruption and
controller admission/export suites pass in 6.34 s (0.56 and 5.77 respectively).
ASan uses `detect_leaks=0:halt_on_error=1`; UBSan uses
`halt_on_error=1:print_stacktrace=1`. This does not establish leak qualification.

The genuine private PipeWire 1.6.2 sanitizer fixture passes: 100,352 bit-checked raw
frames, 8,192 monitoring/sink frames, 392 capture and 413 render receipts, explicit
placement257/evidence8/SaveReopen, and all8,192 native playback samples exactly match
offline EQ output. Missing frames and callback allocation/free/lock audit hits are
zero; SDK, disk reader, controller and owner-only dummy daemon join. No physical,
default-output/volume, user playback, VM or equalizer repository changes occur.
Fresh TSan controller acceptance passes in 5.57 s without suppressions; it covers
actual worker/native-role threads with a simulated SDK, not opaque driver internals.
Old mixer/playback warnings remain retained. Qt and hardware are outside these gates.

## Independent original CI

PR173 original run38333366567 exacta05f290 is terminal with allfour jobs successful:
Linux154/441.98s, Windowsnative81/184.39s, Windowsdesktop20/64.74s and cross-build.
Original job logs/APIs and selected Windows artifacts are retained. This qualifies
that revision's tested scope; a later pass does not explain earlier raw-queue or
clock/pool failures.

PR174 original run38334446715 at98b1593 remains live at the current observation;
its cross-build passes, Windows native fails77/81 in193.61 s, desktop fails19/20
in72.94 s and Linux remains live. Original failure logs and actual generated
projects are retained locally. Owner raw checkpoint2.315 s and desktop raw write
145.68 ms/checkpoint960.97 ms exceed the fixture80 ms queue budget; low desktop
callback lateness excludes a large catch-up burst there. Freeing one slot is not
a complete storage-stall solution. Existing manual-control failure remains separate.

The appended boundary9 collided with a test-only final-publication crash sentinel9.
The selector now restricts ordinary boundary matching to0..8 and selects final
publication explicitly. Service source is unchanged; the one-line test correction
7461a6f passes all raw interruption tests under ASan/UBSan in0.42 s. Original failure
is retained and no common-prefix validation was weakened.

## Next implementation work

Finish current TSan and independent runtime qualification, then implement a verified
copy of an interrupted original's committed finite prefix. Preserve original PCM,
stamps, manifests and receipt tails; use exclusive inactive leases, bounded copy,
source/evidence hashes, no-overwrite publication and explicit review/placement.
Do not mark an interrupted original finalized or relax current admission to accept
partial WAVs. PipeWire paired-prefix coverage and a potentially longer raw-only
prefix must be labeled independently. Continue remaining M2 native playback,
alignment/hotplug/recovery/multitrack gates and later milestones.
