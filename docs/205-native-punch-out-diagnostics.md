# Native punch-out refusal and complete take-stage diagnostics

2026-10-11. M2 progress; full frozen functional, quality, content and native-project
scope, M2–M11, imports, equipment profiles, Windows parity, installation and
European localization remain incomplete.

## Original CI failure

PR140 run38159835904 tested head `a0e09cb1c63e15eac22b72dae94968f1dbaff55a`
against protected MAIN `31db463ba36b568ce83e52a37476f228d9ca3afd`, at merge
`863c7775855b7fe3f535b1adfad0d8e94a2f7ba3`, tree
`6df09d80560c8367ae8ccd4defe93219ff53c16d`. Linux, Windows cross-compilation
and Windows desktop jobs passed. Native MSVC job114530250673 failed one of70
suites in118.22s: `manual-recording-control` failed after19.25s.

The original refused command is Out/revision2/take0 at caller926, workflow3.
Admission was Stopped; transport was CaptureFailed, position8192, occupied1.
This occurs before the completed-group wait. The existing group-wait catch did
not emit the lane/worker/first-error observations at that boundary. The code
correctly refuses commands after a terminal capture fault; this is not evidence
that accepting the command would fix recording.

Three subsequent original20-take workflows passed with the unchanged five-second
acceptance condition. They do not erase the required-check failure or establish
its cause. An8192-frame position alone does not prove queue exhaustion, disk
failure, thread-start failure or a particular disk stall. The earlier completed-
group timeout and interrupted-parent recovery discrepancy remain unresolved.

[Original failure packet](../tests/results/M2/2026-10-11-native-punch-out-failure/summary.json)
retains all four full original job logs, terminal run/job/PR state, tested
parents/tree and16 reviewed source blobs. Logs are losslessly gzip compressed;
decompression preserves their original byte-order marks and line endings, with
original-byte hashes in the summary and compressed-member hashes in the manifest.
No executables or media are included. This original failed head was not admitted;
see the later protected integration below.

## Diagnostic change

The concurrent fixture now catches failures in preparation, submit-in, start
acknowledgement, submit-out, stop acknowledgement, group wait and group verification.
It emits the actual stage/take/identity, existing lane/worker/writer observations,
original callback fault (including failed capture lane and original device clocks),
and any retained first control/disk error. The existing group-wait marker remains
available at its original boundary. The formatter calls only const diagnostic
accessors and the existing const first-error rethrow; it does not service, stop,
consume, reclaim or alter the run.

Each acquire observation remains individually valid rather than an atomic snapshot.
The callback fault is absent when not published; its absence does not exclude a
control-side failure. Writer phaseUINT32_MAX/sequence0 denotes no recorded writer
observation, not an observed disk phase. The original writer coherence flag remains
separate from phase availability. Relative file listing and all diagnostic sinks
are guarded so they cannot replace the initiating exception. The original
exception is rethrown and owner teardown still joins the audio owner and workers.

Production recording/SDK sources, capture budgets,20-take count, callback cadence,
five-second condition deadlines, failure expectations and CI/protection remain
unchanged. No retry-until-green, larger pool or suppressed capture fault is added.

## Deterministic acceptance

The existing unserviced-pool workflow produces an actual terminal QueueFull,
8192-frame retired prefix and original callback fault on lane0. Its subsequent
Out command is refused as Stopped. The new diagnostic reports those actual
observations and no invented retained disk error; normal Stop/join then preserves
and independently verifies all8192 original samples and their durable checkpoint.
This validates the new observations under a known pool fault. It does not identify
the original Windows cause.

The existing active-write-failure workflow verifies the original initiating disk
exception is reported before Stop, remains the same after reporting and teardown,
and preserves independent lanes/checkpoints. A failing output stream cannot
replace or clear that error. Existing concurrent20-take ownership, raw PCM/EQ and
real-time allocation/free/lock checks remain acceptance gates.

## Qualification and next task

Frozen diagnostic source `6b6d6838de7dd04196d62a3c3a6ced1fd97ba25b`, tree
`1254a8dc7e91b25844b389f030477b64e685447e`, changes only the manual fixture
relative to the original PR140 head. Six selected suites pass per profile:

| Profile | Time | Manual-owner checks | Maximum output difference | RT violations |
| --- | ---: | ---: | ---: | ---: |
| Release | 1.65s | 67,262 | 0 | 0 |
| ASan/UBSan | 8.61s | 67,353 | 0 | 0 |
| TSan | 7.55s | 67,261 | 0 | 0 |

Actual check counts vary with owner scheduling. Tests retain floating-point peak
2.14813 and the original complete raw/prefix/worker/delivery/recovery workflows.
The Windows fixture compiles without warnings; no new native Windows execution
or audio activation is claimed. Builds were sequential, single-job, low priority
and used separate directories preserving the preceding qualified binaries.

[Diagnostic qualification](../tests/results/M2/2026-10-11-native-take-stage-diagnostics/summary.json)
binds632 selected exact Git/checkout inputs,18 Linux executable hashes, three
Linux fixture-object hashes and one Windows fixture-object hash, configurations,
commands, full original logs and the source-only delta. Logs are losslessly
compressed; original and compressed hashes are both retained. Existing GCC15
warnings in unchanged protected-MAIN mixing sources are retained with exact blob
provenance; no flags were suppressed. An initial input collector refused the
known PowerShell CRLF checkout; the corrected collector records both hashes and
verifies that single line-ending conversion without modifying files.

The revised fixture subsequently executed in native Windows CI on38161752733:
all four required jobs passed, with70 MSVC suites166.67s and three original20-take
completions. Exact head0d50, tested merge597b418 and53 reviewed blobs were checked
before ordinary protected integration into MAIN7d800f. The [protected receipt](../tests/results/M2/2026-10-11-native-take-diagnostics-protected-main/summary.json)
retains all four original full logs and source/admission/postmerge bindings.
The original failed head remains separate; its subsequent standalone successes
never constituted admission or causal resolution.

Keep the original PR140 failure and prior unresolved roots visible. A later green
can establish only bounded non-reproduction unless new observations identify a
causal correction. Then continue immutable startup consumption through the native
warmup hook, auxiliary state1.1 migration, composed original-clock/full-FIFO PCM,
Stop/EOF/fault/recovery and native/installed gates before enabling the factory.
