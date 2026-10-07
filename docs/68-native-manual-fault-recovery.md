# Native manual recording: fault recovery and unresolved alignment/timing

M2 checkpoint, 2026-10-07 UTC. GPL-3.0-only. No production implementation,
project/journal schema, dependency or frozen parity status change.

## Fixture and contracts

`sc-pipewire-manual-fault-fixture` is opt-in, outside automatic CTest, and uses
actual `PipeWireManualRecording` on explicit owned source/master-sink routes.
`tests/verify_pipewire_manual_fault.py` retains original projects, files, journals,
logs, binary/verifier hashes, scheduler observations and ownership/default checks.
Synthetic mode checks the same group/recovery and independently partitioned output
oracle; it cannot establish native alignment or deadlines.

Nine workflows cover Stop, early Stop, Cancel, early Cancel, Cancel before any
service, source removal, sink removal, an active write failure, and a finalized
hash failure held until every delayed arm's postroll has finished. The held hash
boundary waits only on its disk worker; no waiting enters audio callbacks. It
must not manufacture a short postroll failure as evidence for a retired failure.
Control-side detection may occur after additional continuous playback; no arbitrary
8192-frame fault-notification deadline is asserted.

32 mono arms have independent delays4097/0/41/200, permuted native channels, and
Off/PostEq/recording-only Auto monitoring. A file track plus33 nonflat EQ processors
and a sample-timed EQ change run continuously. Raw float RF64 samples are checked
against delayed per-channel deterministic input; output uses a separate graph
with127-frame partitions. Headroom, accepted extents, actual callback origins,
inactive journals, media hashes and independently committed recovery prefixes are
checked. Raw acceptance may precede a failing callback's mixed-frame advance;
such a suffix must not be clamped or padded to a shared fictitious extent.

Every accepted In/Out command has an applied or terminal reply. Complete, Empty,
Failed and Canceled lanes remain distinct. Interrupted groups require explicit
partial adoption; canceled groups refuse even partial adoption. Recovery is an
explicit new asset/job copy, retaining original media/journals and timing identity.
Grouped Undo/Redo and Save/reopen qualify nonempty recovered groups. An early
cancellation with no durable samples must retain the saved state and must not
fabricate a recovery edit. An empty nonfinalized journal may have no timing origin
although its pipe accepted samples before cancellation; positive durable prefixes
require their origin. Unserviced cancellation must drain its positive buffered
prefix through late workers after callback shutdown.

## Evidence and open native failures

See the [dated receipt](../tests/results/M2/2026-10-07-native-manual-fault-recovery.json)
for exact source/binary pins and per-attempt results. Native successes are finite
owned-route observations, not sustained, physical, Windows or full DAW parity.
Timing qualification requires every source/owner/sink callback's wall time, CPU,
thread resources and actual clock context: p999 below60% of period, maximum below
80%, and no callback ending past its current cycle. No gate is relaxed for failures.

The retained observation history grows from32 to40:

| Observation | Original result and scope |
|---|---|
|33/34 | Early-cancel synthetic assertion incorrectly demanded an origin on a zero-committed nonfinalized journal. Original retained journals prove committed0/phase capturing/origin null; the lane's buffered origin remains distinct.34 retained the unchanged assertion after an ineffective text replacement. |
|35 | Sanitized hash failure reached the fast lane before delayed postroll finished. Original lane0 had22642 accepted frames, not the full24013; timing was still active. The fixture now holds the hash failure until postroll finishes. |
|36 | Native early Stop ended at58505 after control disk startup, with sink failure. Original failed sink clock and some combined assertion terms were unlogged. All three finite/current-cycle timing gates passed; no retrospective sink-gap cause claim. Native early Stop remains unqualified. |
|37 | Native unserviced Cancel retained32 prefixes; lane21/channel19's full24179 samples match the deterministic source delayed by an extra512 frames, with zero mismatches under that shift. Other31 lanes match the expected origin. Original routing metadata was not retained; cause remains unproven. A later passing repeat does not resolve it. |
|38 | Native hash-fault attempt failed before injection: owner maximum13,880,223 ns wall/13,877,858 ns CPU against512/48k (10,666,667 ns), with one current-cycle overrun. Sink clock skipped512 frames without xrun/discontinuity flags. Exact failed clock is retained. This repeats the unresolved mostly-CPU outlier pattern; no assertion of a common cause. |
|39 | Synthetic retired-hash run stopped at101513, beyond the fixture's arbitrary80355 bound. Original logged status12/group1/replies2/sink healthy. Other combined group terms were unlogged. The continued graph is now allowed until its actual prepared end; fixed raw punch extents and full result/recovery checks remain strict. |
|40 | Final sanitizer sink-loss case stopped at49289 before its injected fault (injected0), reaching the fixture's15-second synthetic observation ceiling under variable host/service load. Native deadlines stay strict; synthetic loop/child observation ceilings are now45/95 seconds. Original observation does not prove a processing defect or a cause for native38. |

Original failures, original executables/sources and original files are frozen
before diagnosis/rebuild. Independent analysis of37 reads the retained original
RF64 files; it is not a substituted successful replay. Diagnostic runs additionally
retain up to32 snapshots of only owned nodes, ports and links, and print exact first
raw mismatch and failed sink clock details. Original36/37 lack those fields and
remain explicitly uncertain. All previous32 observations, including sustained
failures, remain linked and unchanged.

[PipeWire latency documentation](https://docs.pipewire.org/devel/page_latency.html)
explains that an async link adds one quantum. That is a candidate to inspect, not
proof of observation37's cause: the original route parameters are missing, and
absence of async properties in a later passing graph does not settle it.
[Filter documentation](https://docs.pipewire.org/group__pw__filter.html) documents
RT callbacks and port-buffer access. No PipeWire fork or global setting change is
introduced.

## Next implementation task

Add bounded, preallocated per-channel source/received-buffer clock markers and
per-stage wall/CPU tracing to the owned native diagnostic, alongside port latency,
route negotiation and driver-cycle snapshots. Isolate the one-port quantum shift
and the mostly-CPU callback outlier while preserving their original evidence. Also
qualify early Stop without letting32 worker constructions obscure its intended
preroll boundary; do not skip pre-shutdown discontinuities or relax deadlines.

Native retired-hash recovery remains unqualified. Then connect canonical Qt manual
controls through one off-GUI serialized control owner with bounded messages,
reliable replies/results and shutdown priority. Independent Windows, sustained/
physical capture, full take/comp/loop/transport policies, X004/X005/X006, all-Europe
localization and all92 frozen contracts remain open. X006's current256-track cap
is unchanged; no fixed product ceiling is intended.

Automated reviewR2 found the new verifier assumed `.cache` already existed. It now
creates the directory before allocating its workspace. An actual clean temporary
checkout with an external frozen binary reproduced the original FileNotFoundError
and then passed the synthetic early-Stop workflow after the fix. No compiled source
or native deadline changed.
