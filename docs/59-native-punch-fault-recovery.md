# Native punch interruption and recovery checkpoint

Date: 2026-10-06. P004 / M2; bounded owned Linux engine workflows, not full
professional recording-mode parity.

## Workload and fault boundaries

The new explicitly invoked `sc-pipewire-punch-fault-fixture` preserves the existing
success fixtures. It uses the production duplex owner, actual read-ahead/disk
workers and project store. The project has one existing file and 32 mono Post-EQ
arms, a signed stereo matrix and flat EQ. Playback starts at 137; desired punch is
`[48150,144164)`. Independent source delays repeat 4097,0,41,200 frames with a
bijective input permutation, exercising separate raw windows and within-cycle
origins. The original session/media are unchanged until explicit attachment/save.

Four cases run before punch-out:

- **Cancel:** join native callbacks, cancel every writer, withhold all original
  take receipts even if finalization raced ahead, retain verified checkpoints.
- **Source loss:** destroy only the owned source node; native route loss retains
  `DeviceLost`, finalizing each independently valid raw prefix.
- **Sink loss:** destroy only the owned sink node; the same terminal policy retains
  input prefixes without requiring the output route to remain present.
- **Writer failure:** lane17 throws `punch-lane17-disk-write` on its disk-side
  `BeforeAudioWrite` boundary after 8192 written frames. The initiating exception
  survives; the other 31 lanes finalize independently. This is a controlled write
  exception, not an actual full filesystem or physical drive failure.

Synthetic route-loss cases explicitly request the terminal fault and qualify the
oracle. Native cases remove actual test-owned nodes. Callbacks, rates, quantum,
default routes, scheduler and hardware are not reconfigured. Fixed admitted audit
storage records complete wall/CPU/thread-resource/current-cycle facts; no new
instrumentation or processing code is added to production libraries.

## Prefix verification and recovery

After native/disk join, every lane's inactive journal is inspected and its entire
durable raw waveform independently checked against the source's timeline signal.
The independently retained native first-playback cycle and per-lane offset derive
all timing-origin fields. Captured, written, durable and rejected counts remain
separate; cancellation/failure does not promise uncheckpointed audio survived.

Recovery creates a new ID/media job per lane, preserving input latency, native
origin and `RecoveredCheckpoint`. Every recovered sample/hash is verified again.
Original journal/media hashes and parsed original checkpoints remain unchanged.
All recovered clips attach at desired punch-in with source offset zero and their
actual durable lengths. Group Undo/Redo and exact Save/reopen preserve the original
file, punch locators and media.

The sink verifier checks every sample of the common complete playback prefix,
including preroll, live/file mixing and float headroom. It records retained output
frames separately; post-terminal sink suffixes are not claimed as verified
playback. This does not weaken the uninterrupted full-output success fixture.

## Results and retained observations

All four optimized synthetic cases and all four ASan/UBSan/LSan synthetic cases
pass. All four optimized native cases then pass serially after owned builds and
tests are terminal:

| Native case | Durable raw samples verified, and verified again after recovery | Stereo common-prefix samples verified | Original receipts/errors | Owner maximum elapsed |
|---|---:|---:|---:|---:|
| Cancel | 720896 | 145408 | 0 / 32 | 2.459814ms |
| Source loss | 755408 | 145408 | 32 / 0 | 2.010938ms |
| Sink loss | 755408 | 145408 | 32 / 0 | 2.544035ms |
| Writer failure | 357981 | 120832 | 31 / 1 | 1.538394ms |

Each case recovers all 32 prefixes. Sample differences are zero, no missing file
frames occur, instrumented callback allocations/frees/blocking locks are zero,
and all roles pass complete finite timing/current-cycle gates without overruns.
Both existing external links and default metadata remain unchanged in every native
case; all owned nodes/links retire. Fifteen altered receipts are refused.

The first synthetic writer-failure run rejected a combined fixture assertion.
Its original per-lane state was not logged, so its exact original failed term
remains unknown. The fixture now permits explicitly reported rejected frames only
on the injected failing lane and requires the corresponding retained callback
fault. A later optimized synthetic run exercises 1024 such rejected frames; other
lanes have none. Original failure logs/source and launch hashes are retained.
An archive helper initially overwrote the Release copy with the identically named
sanitizer executable; rebuilding its frozen source separately restored exactly the
original Release SHA256. This reconstruction and limitation are recorded rather
than described as an uninterrupted archival copy.

All prior 22 observations remain retained. This additional initial fixture
observation makes 23; later success does not assign causes to historical failures.
All 92 frozen contracts and F/Q/C/N axes remain unchanged and unpromoted. All 24
borrowed inputs still match; equalizer repositories are unchanged. See the
[dated receipt](../tests/results/M2/2026-10-06-native-punch-fault-recovery.json).

## Reproduction and remaining work

```sh
cmake --build .cache/build-desktop-release --target sc-pipewire-punch-fault-fixture
python3 tests/verify_pipewire_punch_fault.py \
  --binary .cache/build-desktop-release/sc-pipewire-punch-fault-fixture \
  --mode source-loss --output .cache/punch-source-loss.json \
  --failure-output .cache/punch-source-loss-failure.json
```

Modes are `cancel`, `source-loss`, `sink-loss`, `writer-fail`; add `--synthetic` for
the oracle. Run native fixtures serially after builds/tests finish. Projects and
stdout/stderr are retained on both success/failure. Only an explicitly owned child
can be killed at the supervisor deadline, followed by terminal wait and cleanup.

Still required: native desktop fault/recovery/discovery workflow, process-kill
restart during punch, empty-preroll behavior, interruption between differently
delayed lane windows/postroll, real disk-full/filesystem durability, sustained
native recording, physical latency and independent Windows qualification.
Next product implementation: explicit saved input-latency controls and their
prepared-session behavior, followed by manual punch/Auto monitoring and
tempo/loop/take lanes/comping. No remaining reference workflow is excluded.
