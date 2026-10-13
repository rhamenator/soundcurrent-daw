# Completing native startup for finite short output

Checkpoint245 fixes an actual short-monitor-output startup deadlock. The full
professional-suite goal remains active; hardware timing/alignment and unrelated
manual recording or fixture-timing failures are not declared resolved.

## Failure and implementation

Previously startup permission required at least the entire native output capacity.
A planned2,400-frame output could finish producing audio below a4,097-frame native
capacity. Capture stopped at its horizon, but rendering continued requesting
warmup silence until a clock failure. The original failed recordings and metadata
remain retained in Checkpoint244.

Processing now publishes an explicit optional **finiteFrames** receipt after:

1. Every pending/queued DSP output block has been published.
2. The conversion buffer has reached its planned end.
3. The published audio count exactly matches that end minus the admitted output
   start, is positive, and is less than native capacity.
4. The output bank's producer EOF is published before immutable startup readiness.

Normal full-buffer readiness remains unchanged and carries no finite marker.
Handoff validation rejects zero, mismatched and non-short finite markers while
retaining the real source statistics and original admission tuple. No source
statistics, fit confidence, clock age, timeout or native capacity is fabricated.

Both native WASAPI and PipeWire consumers acquire this receipt and require actual
output EOF plus an exact bank publication count before authorizing a short first
lease. An open producer remains refused. Final output fill copies actual audio,
zeros only its native slack and retains the actual finish/queue-return evidence.
Zero-size native leases remain invalid and cannot consume audio. This does not
claim a zero-length raw take is finalized or physically audible.

## Versioned evidence

Owned native recordings write schema **1.5**; the new finite marker is an explicit
field of startup receipts. Legacy standalone writers still default to1.3; readers
retain0–4 support without inventing finite permission. A legacy writer refuses to
encode finite readiness. Unknown6 is refused. Native completed-outcome validation
requires the finite count to match consumed audio and the declared monitor end.

PipeWire owner manifests now use **1.3**, mapping to outcome revision2. Intents
retain the1.2 storage declaration shape. Older outcome revisions0/1 and manifest
versions0–2 remain readable; unknown4 is refused. Finite completed counts must
match verified queued/consumed audio and actual completion returns.
No raw capture journal, raw PCM, output-capacity declaration or clock domain is
reinterpreted by this change.

## Acceptance evidence

Source `3a5490ee18016e6947962ccc24ef74ddd6d59601`:

- Initial Release selection6/6 in33.24 s: owned PipeWire workflow, its durable state,
  native storage workflow, original native state/migrations/process interruptions,
  composed native startup and bounded processing startup.
- Composed native startup19.59 s and bounded startup1.04 s include contradictory
  finite-marker refusal, open-producer/zero-lease refusal, and finished one-frame
  source through actual simulated native fills of1,17 and4,097 frames. Audio
  headroom remains2.5; every final slack sample is zero. Consumed-frame count,
  native completion and slack receipts are exact; callback allocation/free/lock
  audit remains zero.
- Expanded platform workflow2/2 in16.20 s: native storage9.03 s, owned PipeWire7.17 s.
  Native monitored outputs1 and2,400 frames complete through capture startup,
  disk pause, saving/reopening and forged finite-receipt refusal. Additional
  44.1 kHz capture/48 kHz output conversion completes short2,400-frame output.
  The established8,192-frame case and deliberate small-buffer overflow remain.
- PipeWire short output1 frame completes, preserves exact raw capture bits and
  actual native return facts, and reopens the same immutable finite receipt.

These counts describe monitored output. Raw recordings retain their actual
captured preroll and stamps; no hardware alignment or raw/monitor duration equality
is inferred. Tests use genuine disk/processing/controller implementation with
simulated SDK/native-role threads, not physical devices.

Fresh ASan/UBSan core startup build64steps: **2/2 in12.59 s** (native startup2.94 s,
bounded startup9.64 s), leak detection enabled and halt_on_error for both sanitizers.
This includes finite permission/EOF/fill/slack and concurrent handoff tests; the
media-disabled profile does not claim sanitized recording persistence or disk I/O.
New-source independent Windows CI remains required.

[Evidence index](../tests/results/M2/2026-10-13-finite-native-startup/index.json)
contains59 compressed text/configuration/metadata members,619 source-input hashes
and8 local executable hashes. Original native failure logs/artifact metadata are
retained; original ZIPs remain local. No PCM/executable/archive payload is published. No GUI source changes, VM, device selection, volume change or
physical audio test is needed for this checkpoint.

## Separate retained Windows failure

Original PR178 source3a80116/run38341742356 is fully terminal: Linux158/158
in462.40 s, Windows desktop20/20 in76.15 s and cross-build pass. Windows native
84/85 in209.88 s fails only native-monitor-startup; its separate concurrent-owner
diagnostic completes successfully. All original job logs/metadata and Windows
archives are retained. It has a failed Windows native startup
case after completed48 kHz and44.1 kHz owner cases. Its original five-second
observation reports96 kHz raw182400,190 packets/copies, output0, no processing
fault, and nominal fixture coordinate1.89 s. Only190 nominal10 ms cycles completed
during that wall-clock wait. This is distinct from finished short output; the
finite-startup change does not claim to fix its timing/cadence cause. Retain its
original artifacts and inspect actual fits/dispatch/worker evidence before repair.
Do not enlarge the deadline or invent confidence.

## Next concrete work

Retain original new-source CI evidence and independently qualify Windows saved1.5,
PipeWire1.3 and short output. Investigate the original96 kHz fixture cadence/fits.
Admit and expose the manual capture reserve through its existing bounded API,
retain small-pool failure checks and prove exact accepted prefixes within/beyond
disk stalls. Continue interrupted-original copy/review and remaining M2–M11,
quality/content/compatibility, European language and installer/hardware work.
