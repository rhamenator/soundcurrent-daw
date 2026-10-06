# Recording checkpoints with one checked audio flush

Date: 2026-10-06. Scope: M2d4c4; full native duration qualification remains open.

## Retained long-run failure

The unchanged 1800-second native attempt from `5b23173` stops at about 1234.6 audio
seconds. Lane 25 exhausts its 118 slabs (483,328 frames). It retains 59,260,928
frames and explicitly rejects 1,024; the other 31 raw lanes retain 59,261,952 each.
The received and previous owner clocks are contiguous, with no reported XRUN or
discontinuity. The sink retains 59,261,952 frames without a clock failure.

Read-only verification checks all **1,896,381,440 raw samples**, including unequal
suffixes, and **118,521,856 stereo samples** over the common raw extent, exactly.
It preserves floating-point overs (peak 4.51584) and all 105 original files, hashes,
journals and canonical state. The extra 1,024 sink frames are outside that common
output oracle; no full-sink equivalence claim or trimming follows.

All three callbacks have complete wall and thread CPU coverage and pass the
unchanged current-period timing thresholds. Owner p99.9 is 3.798280ms; maximum wall
16.372813ms is 76.7476% of the 1024/48000 period, with 16.370566ms thread CPU.
That maximum occurs far before the storage fault and does not diagnose the earlier
196-second run's callback outlier. CPU charged to the thread can include system work.

All 33 disk observers have complete phase pairs. Checkpoint work repeatedly exceeds
the roughly 1.024-second audio spans it commits; backlog accumulates rather than
one stall exceeding the ten-second reserve. Lane 25's maximum audio flush takes
1.970521478 seconds before the fault, while queued slabs rise from 57 to 80.
Some journal/flush maxima occur during draining after capture stops; they cannot be
labeled the initiating phase. Exact phases, clocks and before/after context are in
the evidence. Underlying storage/scheduler causes remain unproven.

A read-only host snapshot identifies an ext4 root filesystem on a Crucial BX500 SSD,
not a rotational drive. The two-socket Xeon uses intel_pstate powersave, broad thread
affinity and NUMA balancing. No power, scheduling, affinity, memory-limit or device
setting changed. These contextual observations were not taken at the failure and
do not establish its cause. Controlled competing load and physical audio remain
unqualified; production memory-lock reporting remains an explicit stub.

## Change and durability ordering

The pinned libsndfile 1.2.2 [sf_write_sync implementation](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/sndfile.c)
only calls `psf_fsync`. Its [file I/O implementation](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/file_io.c)
issues `fsync` or `FlushFileBuffers` and discards the return value. The
[API documentation](https://libsndfile.github.io/libsndfile/api.html#write-sync)
describes forcing OS cache data to disk. This path performs no separate PCM buffer
flush. Exact local source hashes and runtime version are retained in the evidence.

Remove that redundant call from `AudioFile::checkpoint`. Keep the existing header
update and libsndfile error check, then the application's **checked** platform flush.
Only after it succeeds may the journal be written, flushed, renamed and its parent
directory flushed. Close/finalization remains checked. No cadence, project schema,
reserve, slab, RT callback, graph or sample gate changes. A dependency update must
re-audit this assumption. Removing duplicate work is justified independently of
whether it fixes sustained throughput on this host.

## Acceptance and limitations

A Linux test wraps application `fsync`, selects only the owned audio inode and
returns EIO on its second checked flush. The writer marks failure, does not reach
after-flush/journal publication, retains the first 256-frame committed checkpoint
and reports the failure to capture. The file contains 512 exact samples; recovery
copies only the committed 256 with new identity/recoveredFrom, preserving original
media and journal hashes. No flush occurs in the marked audio callback.

The wrapper does not intercept libsndfile's shared-library calls. It tests actual
application flush error handling, not the number or cost of library sync calls.
There is no power-cut or device-firmware durability claim. The full Debug suite
passes all 27 groups. The first sanitizer suite passes 26 groups and times out
awaiting completed-recording close in desktop-ui (line 1183); a serial isolated
recheck passes without a code change. Preserve both receipts; the transient cause
is unresolved and the full sanitizer suite is not labeled a clean pass. Broader native
short-run and independent copied-recovery receipts are in the evidence manifest.

The first 20-second normal probe stops after about 13.5 seconds on another
skipped sink cycle. All 20,742,144 raw samples and 1,292,288 common stereo samples
verify exactly, with all originals unchanged and no pool exhaustion (max queue 1).
Individual callback gates pass, but source and owner maxima share the missing
cycle: source starts 5.399437ms after its timestamp and ends at 13.834610ms; owner
starts at 14.598437ms and ends at 22.007843ms versus a 21.333333ms period. Native
cycle timestamps have jitter; this is evidence of late graph scheduling/processing,
not a proven physical deadline or a root cause. Flush and journal maxima are only
49.753614ms and 51.096911ms. Preserve this failure alongside any bounded retake;
passing individual wall budgets does not establish complete graph continuity.

A bounded unchanged retake passes 20-second normal and four-second stall absorption:
each checks 30,720,000 raw and 1,920,000 stereo samples exactly, Save/reopen, float
overs and complete callback wall/CPU coverage. Owner maximums are 11.356907ms
and 4.868794ms respectively; the absorbed lane queues 46 of 118 slabs. Deliberate
12-second stall exhausts lane 17, explicitly retaining unequal prefixes with
17,988,608 full raw and 1,122,304 common output samples exact. Independent-copy
recovery verifies every raw sample, new identity/recoveredFrom, timing origin,
aligned clips and Save/reopen/previous-save backup; all original files and copied
source media/journals remain unchanged. These short successes do not resolve
the preceding skipped cycle, UI close timeout or sustained storage fault.

Next: the unchanged 1800-second native
workload, serial after all build/test/read
handles terminate, with exact samples and existing timing gates. Preserve all
fifteen unresolved historical observations. If backlog persists, investigate
coordinated checkpoint publication or measured filesystem service cost; do not
hide it by reducing the workload, relaxing durability cadence or growing reserve.
Linux physical/load/filesystem/power-loss/unload, Windows native/Qt/install,
professional workflows, imports, equipment profiles and European localization
remain required. Frozen reference F/Q/C/N contracts stay unpromoted.
