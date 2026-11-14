# Protected comp assembly, publication and model adoption

2026-10-15. Source `2dd1b089c1c68db69883c15fb235653d7775f44a`.
The full DAW goal remains incomplete. These are exercised engine/model workflows;
the desktop's default selection policy remains unchanged.

## Actual producer and assembly path

The new acceptance bank uses the existing protected-pair policy with real original
nominal renders. It prepares and verifies owned stretch-worker requests, preserves
the original semantic warp, adopts verified nominal output with Undo/Redo, then
reopens the project before preparing the expanded protected span requests.
The placeholder nominal files from the raw-input fixture are replaced by actual
owned worker output and receipts before assembly is admitted.

Five pairs cover protected/protected at 44.1, 48 and 96 kHz, protected/uniform at
48 kHz and protected/existing media at 48 kHz. Processor and protocol identities
are asserted for both sides. Existing media creates no nominal/span/output job.
The actual pipeline checks the original receipts, both needed child completions,
trusted selection, allowed blend regions and source freshness. The assembly bank
independently calculates blend weights and compares all samples outside blends.
Matching this calculation is not independent acoustic quality qualification.

The same published files pass complete PCM recomputation and the separate media
scope reviewer. The canonical model adopts the verified pair, supports exact
Undo/Redo, saves and reopens it, then compares live engine output in 193-frame
callbacks with offline WAV export in 127-frame blocks. Floating headroom remains
above unity. This uses the real application engine/model APIs; it does not enable
the desktop entry or qualify hardware audio.

## Interrupted publication and corruption

For each pair, three separate processes abruptly exit with code 86 at observed
publication boundaries: after the first file closes, after the first file is
published and immediately before the completion marker. They flush their boundary
report, then call `_Exit`, bypassing C++ destructors. A fresh process reloads the
trusted request and requires Interrupted state, no verified result, no completion
marker and refusal to resume/replace that job. It checks the exact number of files
published at each boundary: zero, one, then every needed output. All original raw,
nominal and span input bytes remain unchanged.

These 15 abrupt exits test process failure and lock retirement. They do not simulate
power loss, torn storage writes or operating-system crashes. The existing callback
failure/cancel cases also run, as do active-writer refusal, changed intent and
source receipts, output limits and counterfeit PCM with internally consistent
hashes. Permitted-region changes still fail complete recomputation. Cancellation
and errors never promote a partial pair into completed audio.

The shared fixtures now preserve context/warp when preparing nominal renders,
handle null existing-media sides explicitly and use Windows file API paths.
Legacy uniform scenarios retain their default setup. The original stretch protocol
still lacks an executable hash field; this test records the actual nominal binary
hash independently and does not claim that the old protocol binds that hash.

## Qualification and evidence

Two Debug suites pass in 94.712 seconds; four Release suites pass in 63.148
seconds, including legacy uniform publication and protected-pair recovery. Source
and binary hashes remain stable, and selected build targets had no outstanding
work before testing. Each new bank records 110 reaped processes, including nine
nominal workers, nine span workers, five canonical pair adoptions and 15 abrupt
publication exits with fresh-process recovery.

Each configuration checks 929,664 complete assembly samples, including 929,520
bit-exact unblended samples and 144 samples checked against independent blend
weights. Live/offline comparison covers 983,040 samples. All 14 assembly/export
file hashes match between Debug and Release; six legacy assembly hashes also
match checkpoint 302's corresponding fixture family. The three changed C++
fixtures pass MinGW syntax checks without diagnostics. This does not establish
native Windows execution of the new bank.

[Evidence index](../tests/results/M2/2026-10-15-protected-comp-assembly/index.json)
records original projects, owned requests/receipts, complete PCM, all process
stdout/stderr, abrupt-exit recovery, qualification source/binary hashes and audit
scripts. The archive contains 3,382 original files represented by 869 unique
payloads, 19,590,237 bytes, SHA-256
`00d452f4b3712281d0c8f062db86972b5ab509c179834f7822f872d886931d40`.
All payload hashes and ZIP CRCs were verified.

No product DSP, publication algorithm or desktop policy was changed;
this increment exercises the newly admitted protected path through the existing
assembly and canonical-model implementation.

A first Release probe passed before the final boundary-count, processor-selection
and unchanged-input assertions. The initial compile found a test-fixture comparison
between a string view and JSON; it was corrected to an explicit string extraction
before source commit and final qualification. Both diagnostic logs remain retained.

## Remaining gates

Connect the explicit protected policy to desktop transition preparation and add
actual timeline creation, Review/Apply and refusal workflows. Keep original
uniform behavior stable. Require native Windows confirmation and current installed
preview qualification separately. Repeated protected-history rendering, fractional
and changed clocks, broader multichannel cases and independent gaps/joins/music/
phase quality remain open. See ADR 104. M2–M11/F/Q/C/N/X004/X005 and all-Europe
remain incomplete. No installed app, audio route, VM or equalizer was changed.
