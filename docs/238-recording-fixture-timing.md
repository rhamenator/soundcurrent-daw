# Recording fixture timing and original failure evidence

Checkpoint 238 measures the existing simulated recording cadence and journal work.
It does not claim the raw-queue failures are fixed. Full professional-suite parity
remains active.

## Actual retained failures

Original PR171 run 38330869925 at 6428369 finishes with Linux 153/154 in 440.89 s,
Windows native 80/81 in 262.05 s, Windows desktop 19/20 in 63.28 s, and cross-build
pass. Actual failed projects are retained locally for Linux and Windows controller
cases and the Windows panel case, including original WAV/packet/receipt bytes.
Public source evidence contains text/metadata/hashes, not PCM or archives.

The panel failure records 19,200 raw frames/40 accepted packets, no startup and no
processed audio. Both controller failures likewise show raw QueueFull (14), no
startup, and secondary ProducerFailed clock handoffs. Activation, disk, storage and
recovery exception flags are false. This proves raw-queue rejection; it does not
prove the original OS scheduling or disk-service cause. Calling it a disk exception
or a clock-fit failure would misstate the retained outcome.

Original PR170 run 38330442681 is also retained. Linux passes 151/154 in 390.36 s,
failing recording ownership, panel and manual-control tests; Windows native and
cross pass, desktop fails the PipeWire panel. The manual-control submit-out refusal
is a separate unresolved issue. Older missing failed project bytes are not recovered
by future retention improvements.

## Measurements and bounded retention

The shared test fixture records actual elapsed callback dispatch timestamps in
fixed arrays of 1,000 entries per native role, inspected only after joins. It
measures maximum dispatch lateness against the unchanged absolute ten-millisecond
schedule. Failure paths save the bounded timestamps as CSV inside retained projects.
Journal hooks measure raw audio/stamp write span and the interval from completed
raw checkpoint to paired-manifest publication. The latter excludes the raw flush
and must not be described as total checkpoint duration.

Instrumentation is outside callback audit guards and in test/control/journal work;
production callbacks and the shared engine are unchanged. Existing queue counts,
clock confidence, deadlines and completion criteria remain intact. Linux artifact
retention now accepts the exact generated owner marker/root as well as controller
and panel roots. UTF-8 markers preserve Unicode paths.

A bounded standalone scheduling experiment shows overdue absolute sleep deadlines
can replay callbacks less than 0.1 ms apart, while a minimum-spacing schedule keeps
the declared ten-millisecond spacing. This demonstrates a fixture hazard, not the
original runner cause. No cadence correction is made before measurement evidence.

## Qualification and next task

Source cc0d1d5 binds 839 selected inputs. Current Release ownership/controller/Qt
qualification results are recorded in the bound summary. Old mixer/playback warnings
remain visible. Native Windows timing measurement is pending original PR172 run
38332233927, which is kept intact before correction.

Next inspect actual measured dispatch/write/publication spans and retained failure
projects, then correct a proven fixture or production defect without weakening
acceptance. Continue native playback of reopened, admitted takes through the existing
read-ahead/shared engine and an owned private sink; compare live/offline samples,
placement, float headroom, missing frames and owner retirement independently.
