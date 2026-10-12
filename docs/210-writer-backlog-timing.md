# Original writer backlog and measured write spans

2026-10-11. M2 recording investigation. Full-suite parity, Linux/Windows delivery,
installation and European localization remain active and incomplete.

## Change and timing domains

The manual recording fixture's existing disk/construction instrumentation now
retains the original `RecordingWriterObservation.backlog`: ready slabs, acquired
frames, queued-frame upper bound, capacity and the `hasBacklog` flag. The control
owner reads a fixed atomic snapshot with at most eight attempts. It never polls
the mutable capture pipe, authorizes reclamation or replaces the initiating error.
An unpublished/busy observation is explicitly marked incoherent.

The fixture also records local steady-clock observation times, an in-flight
WriteHashBegin timestamp, the latest completed begin/end pair and the maximum
completed span across that fixture. Snapshot reads contain no clock calls,
allocation, logging or blocking locks. Time sampling and formatting occur only
on construction/disk/control owners. These local intervals are separate from
sample positions, original device/QPC clocks and project alignment.

A write span includes the existing boundary hook, scheduling, audio write and
hashing. It is not syscall-level disk latency and does not identify antivirus,
filesystem or scheduler activity. Backlog fields describe the original phase
observation; they do not claim to be a live queue count during a blocked write.
Unknown construction backlog remains explicitly absent. Aggregate maximum span
is labeled for the fixture rather than being attributed to an unrelated take.

Only `tests/manual_recording_tests.cpp` changes implementation. Production
recording, capture capacities, deadlines, original 20-take/five-second acceptance,
three native standalone runs, callback cadence and persistence formats are
unchanged. Failure output adds the measured facts; successful standalone output
retains its original completion marker and adds a timing row.

## Controlled acceptance

An actual worker is deliberately held at its first BeforeAudioWrite boundary.
The test verifies original acquired-frame/capacity facts, unchanged observations
during the hold, bounded snapshots under RT auditing, measured elapsed time and
capture QueueFull while the write remains pending. After releasing the worker,
it joins, persists the entire 8,192-frame raw prefix and compares every sample
with the independent source. The completed span includes the observed hold.
Busy snapshot, failed diagnostic sink and existing held-finalization behavior
remain qualified. This establishes the controlled fixture mechanism; it does not
infer the earlier native Windows failure's cause.

Frozen source `3ed02b44b206250a2726cefb59d2190c1e33cc9e`, tree
`5db204da4694be29d1cd708af2fab7e52c3da6c2`.

| Profile | Manual workflow time | Checks | Controlled hold age | Completed maximum span | RT violations / PCM difference |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release | 1.14s | 75,531 | 21.803005ms | 22.383900ms | 0 / 0 |
| ASan/UBSan | 4.07s | 75,618 | 30.661382ms | 31.099345ms | 0 / 0 |
| TSan | 2.85s | 75,529 | 26.425955ms | 26.838419ms | 0 / 0 |

Counts vary with actual wait observations. Each profile runs the full existing
manual recording workflow plus the controlled test and original concurrent
owner. A separate Release invocation verifies the original standalone protocol.
The changed fixture compiles for Windows with no warnings; that object has not
been executed natively.

To reduce disk work, sanitizer fixtures use newly compiled test/audit objects
and 19 unchanged read-only production archives from checkpoint209's qualified
profiles. All other 635 input bindings are unchanged; archive hashes, generated
CMake commands and actual compile/link commands are retained. The Release target
was built through CMake in its own directory. Earlier qualified build directories
and executable hashes remain intact. A helper initially expected 20 archives;
the actual CMake link has 19. Its original script, commands and logs are retained;
correcting that count changed no source, library, test limit or runtime behavior.

The [qualification packet](../tests/results/M2/2026-10-11-writer-backlog-timing/summary.json)
retains source/checkout bindings, configurations, commands, executable/object and
read-only archive hashes, complete logs and the original helper failure. Its
[offline verifier](../tests/results/M2/2026-10-11-writer-backlog-timing/verify.py)
checks the original compressed bytes and observed scope. No executable, object,
archive or audio file is uploaded. Existing unchanged mixing-source/compiler
warnings remain in the original development build log.

## Native evidence and next gate

All four required jobs passed on the preceding draft PR144 run38174473151.
Native job114573793516 passed 71 CTests in 183.10s and
three actual original 20-take/five-second standalone runs. Its artifact confirms
12 composed startup/state runs, including four mono-to-stereo cases and actual
legacy startup migration, plus two actual matrix-publication process interruptions.
The [exact CI/runtime packet](../tests/results/M2/2026-10-11-native-channel-state-windows-runtime/summary.json)
binds the tested merge/tree, 162 reviewed blobs, four complete original job logs
and seven selected original native artifact logs. It tests source f8c6b8b, before
the new writer timing diagnostics. The native SDK
operations are simulated; physical devices and the ordinary factory remain open.

PR143's earlier QueueFull/WriteHashBegin failure remains unexplained. New timing
and backlog evidence must run on native Windows and be inspected if that failure
recurs. Next qualify these original diagnostics in fresh native CI, then use the
observed pending phase, elapsed span and retained backlog to identify a concrete
correction. A successful run does not establish the old intermittent cause was
fixed, and the full DAW goal remains incomplete.
