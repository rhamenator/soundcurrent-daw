# Controlled native startup: allocation before publication readiness

Scoped mechanism checkpoint, 2026-10-07 UTC. See the
[receipt](../tests/results/M2/2026-10-07-controlled-native-startup.json).
Full goal incomplete; no production adapter fix is delivered here.

## Experiment

The owned 32-channel source uses the installed PipeWire 1.6.2 library. A test-only
secondary `add_buffer` listener holds its first buffer notification for source
channel 23 on the mainloop until a genuine audio callback observes that allocated
buffer while synchronous IO is unavailable. The audio thread never waits. This
extends a negotiation window through public callbacks; no IO/status/sample fields,
private queues or PipeWire implementation are patched.

Exact upstream [filter.c](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/filter.c)
seeds output buffers before returning from buffer configuration, and the getter
immediately queues each dequeued output. Its process skips publication when IO is
absent. Exact [impl-port.c](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/impl-port.c)
and [remote-node.c](https://github.com/PipeWire/pipewire/blob/1.6.2/src/modules/module-client-node/remote-node.c)
show the client negotiation path. These sources are assessed and hashed, not
incorporated into the product. The newer
[event documentation](https://docs.pipewire.org/structpw__filter__events.html)
distinguishes mainloop notifications from RT processing; installed headers/source
and actual off-RT gate entry are retained independently.

Two declared modes:

- `observe`: invoke the actual getter normally. It returns the first allocated
  buffer before IO can publish it; the source fills that buffer with its actual
  current-clock signal.
- `defer-unready`: on source channel 23 only, defer the getter while public IO is
  unavailable and an allocated buffer is visible. This deliberate counterfactual
  returns no view and records `api_suppressed`. It continues deferring after the
  hold ends until the IO notification arrives. All other queries remain unchanged.

The original and counterfactual executables/sources are frozen separately. Before
the counterfactual, deferral was extended from the held interval to the entire
unready interval and its unit test added the post-release/IO-ready transitions.
The observe branch's behavior and every production source/library remain unchanged;
the two executions are not presented as identical executable bytes.

## Results and preserved original 47

| Retained evidence | Original getter | Deferring getter |
|---|---:|---:|
| Gate entered off RT/completed/released |yes /yes /yes|yes /yes /yes|
| First allocated buffers /IO known |1 /false|1 /false|
| Selected actual getter skipped |false|true|
| Selected returned buffer known |true|false|
| Actual rate /quantum |48 kHz /256|48 kHz /256|
| Advancing owner callbacks |194|194|
| Current-cycle correspondence on all 32 channels |fails on23|passes|
| Durable raw samples independently examined |37,696|37,696|
| Affected lane 17 samples |all 1,651 match extra −256|all 1,651 match expected|
| Full recovered samples verified |not completed|37,696|
| Full nonflat output oracle |not reached|99,328 samples|
| Owner wrapper max wall /CPU ns |694,591 /690,946|891,219 /886,715|

Original 47 is the deliberately extended startup run. It fails the original raw
waveform oracle on lane 17/source23. It is preserved before diagnosis with its
complete generated project, original logs/routes, exact source/executable manifest
and independent analyses. All 194 advancing received marker rows on23 match the
previous cycle; every other channel matches the current cycle. All 1,651 affected
durable samples independently match −256; all other positive lanes match expected
timing. No compensating offset is applied. The initial getter returned public
buffer slot 0 while its IO was absent, at the gate's exact native clock.

The counterfactual has one explicitly suppressed SDK query. Source-generated
markers, received markers, full raw/recovered/output, priority interruption,
clock/cycle/deadline, RT and owned-route gates pass independently. This verifies
that the allocated-but-unpublishable query can cause the controlled delay and
that deferring it avoids that mechanism. Private FIFO depth/publication are not
directly observed. Ordinary handoff qualification rejects API suppression;
controlled analysis requires an explicit flag and its joined gate file.

Debug/Release and ASan/UBSan/LSan pass both modes, including release without IO
readiness and later actual IO readiness. The fake provider verifies exact SDK
call counts, untouched channels, no RT waiting/allocations/frees/locks and joined
clock/query serialization. Existing marker/handoff regressions also pass.
21 altered controlled-pair evidence cases and40 altered ordinary handoff/marker
cases are refused without rerunning audio. An initial mutation harness inserted
an unused `allocations` key; that development failure is preserved separately,
then corrected to the existing `rt_allocations` field. Runtime observations remain 47.

Original 46 and 37 still lack their original IO/queue terms. This controlled result
does not retroactively establish their causes, or resolve earlier CPU/sustained
failures. There is no native Windows/physical claim, F/Q/C/N promotion, new
dependency, persistent schema change or change to equalizer working trees.

## Reproduction

Build `sc-startup-gate-tests` and run CTest `native-startup-gate-observe` and
`native-startup-gate-defer`. Build the opt-in
`sc-pipewire-manual-startup-fixture`. Native targets require the existing Linux
PipeWire/media dependencies and are excluded by hosted `SC_BUILD_PIPEWIRE=OFF`.

Set `SC_NATIVE_STARTUP_POLICY=observe` or `defer-unready` when invoking
`verify_pipewire_manual_fault.py` with that binary and `--mode early-stop`; provide
both output and failure-output paths. Observe is expected to fail on its raw
samples; preserve its terminal evidence before analysis. Native work follows
termination of all owned local CPU builders/tests/producers.

`verify_native_startup_pair.py` takes the original project, counterfactual receipt,
independent original raw analysis and an output path. Run the pair mutation tests
with the same inputs. `verify_native_port_handoff.py` requires
`--allow-startup-intervention` for these controlled projects. The archive preserves
the original independent raw-analysis script and all inputs; do not regenerate
originals by replaying audio or silently apply their diagnostic shift.

## Next implementation

Implement production output acquisition against actual publication readiness for
every configured channel, with prepared stable port metadata and public IO/buffer
notifications. Preserve ownership through callback completion and certify returned
native buffer extents before exposing capacity-backed views. Avoid acquiring and
queuing output while IO is absent or holds unconsumed data. Keep allocation, blocking locks and object construction/retirement outside RT.

Trace logical channel acquisition attempts separately from actual SDK calls:
deferred ports must retain their channel ordinal, rather than shifting every
later marker. Do not merely skip calls beneath the current ordinal-based wrapper.
Qualify the production adapter against the same forced startup, sparse readiness,
unsupported/removed IO, buffer extents, ordinary native recording and unchanged
waveform/cycle contracts. Then finish the bounded Qt manual controller and
late-Cancel/monitoring Undo workflows. X006 coordinated track scaling and all
remaining frozen-reference/platform/localization workflows remain required.
