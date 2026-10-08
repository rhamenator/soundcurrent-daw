# Recording first-fault diagnostics

Implementation checkpoint, 2026-10-08 UTC. Scoped Linux qualification is recorded
below; guide 90 includes a newer packaged diagnostic candidate separately from
the earlier recording/EQ/export candidate.
This extends recording failure visibility for useful previews; full recording
alignment, sustained native operation and Windows workflows remain required.

## Receipt and ownership

The framework-independent `AudioBridge` retains a fixed-size `AudioBridgeFault`
separately from its lossy meter queue. It distinguishes invalid quantum/rate/buffer,
xrun/discontinuity, position overflow, clock identity change, unexpected position,
timing-origin refusal, capture failure and processor failure. Callback receipts
contain the rejected clock, previous accepted clock, engine/capture positions,
input/output channel counts, proven buffer capacity, prepared bounds and generation.

Only the successful terminal-state CAS publisher writes the payload. It publishes
readiness with release ordering; control readers acquire readiness before copying.
The payload is immutable for the owner's lifetime, so later callbacks, control
notifications, a full meter queue or normal stop cannot overwrite it. A status can
become visible briefly before its receipt; the recording owner joins native
callbacks/control notifications and rereads telemetry before retirement.

Control requests use the same terminal-state arbitration and explicitly have no
callback/previous-clock values. They never inspect mutable audio-owned history.
Normal Stop and completed ranges do not fabricate a fault. The slot is charged
through the existing `sizeof(AudioBridge)` resource admission. Callback work uses
fixed storage, bounded comparisons and atomics; formatting and GUI work occur on
the control/UI side.

## UI and retained scope

The native single-track recording owner exposes the receipt through controller
telemetry, including after native/writer join. The GUI presents a contextual
translated reason with locale-formatted current/previous clock details. The
controller marks which diagnostic originated from the backend; later writer or
take-verification errors retain their own messages. The raw take/journal and
canonical project are not changed by this diagnostic data.

Fourteen additional source messages bring the desktop catalog to 539 keys.
Non-English catalogs remain unverified drafts with 8 translated entries each;
new messages use English fallback. An external read-only localization audit
reported the same coverage totals, but its 33 input hashes no longer match after
the block-size wording correction. It does not qualify this source cohort,
linguistic correctness, plural rules or live localized UI. The in-repository
catalog generator/audit remains authoritative for the current input set.

The receipt is retained in the current recording session and test evidence. A
portable diagnostic sidecar for reopening a failed job is still required; the
current recording journal continues to retain its existing end reason and timing
origin. Existing multitrack/manual-punch diagnostics remain separate contracts.

## Acceptance and next action

Verify each clock cause, initial rate/quantum/buffer faults, meter-queue saturation,
concurrent readers and control/completion arbitration, receipt stability after
join, RT allocation/free/lock audit, controller take preservation, GUI explanatory
text and writer-error precedence. Reproduce the original private audiotestsrc
failure using the production recording owner and retain both clocks and raw prefix.

Do not use nonzero sample amplitude as an input-validity test. Genuine silence is
valid audio. Do not trim takes or move timeline placement to hide the observed
2,048 leading zero frames. Next, distinguish native missing-buffer substitutions
from valid upstream zeros, define a tested first-valid-input/alignment policy,
persist fault diagnostics outside callbacks, and qualify a new binary/source pair.

## Qualification checkpoint

Full Linux Debug passes **62/62, 190.40 s**, including the GUI explanation and
controller/native-before-writer retirement paths. Additional timing-origin,
processor timing-overflow and writer-error precedence assertions pass in a focused
**2/2, 1.42 s** run without changing production inputs. Affected ASan/UBSan/LSan
checks pass **5/5, 20.10 s**. The [receipt and evidence capsule](../tests/results/X007/2026-10-08-recording-fault-diagnostics.json)
retain input hashes, test scopes, the failing fixture, private executables and
native clocks/raw prefix. CRC and all 2,655 logical entries are verified;
the archive is 53,221,872 bytes.

A normal-user private PipeWire 1.6.2 reproduction through the production recording
owner retained **1,024 raw frames** and the first precise fault. Clock **29** changed
cycle **1 → 2** while its position stayed **0 → 0**, with duration **1,024** and rate
**48 kHz**; xrun/discontinuity flags were false. The next required position was
1,024. The receipt correctly reports `PositionJump` at engine frame 1,024 and
survives native/writer join. This diagnoses the reproduced audiotestsrc route,
not the old opaque capture or original native71 independently. It does not resolve
the separate 2,048-frame startup silence. The raw prefix/hash and private graphs
were verified, owned nodes/links retired and host defaults/links remained identical.

One fixture compile failure used the wrong Asset member name; the failing source
and build log are retained. No VM, host package, physical audio or equalizer
modification occurred, and no binary release was uploaded.

The read-only reuse audit found four changed working-tree localization/profile
inputs in each equalizer, with no retained-snapshot errors. These changes include
standard Qt action translation and stricter catalog extraction/validation. Their
exact bytes/hashes were captured for a separate review; existing DAW adaptations
stay pinned until reviewed and qualified. Review/adapt that upstream work before
the next reuse-dependent localization/profile milestone. Do not replace the
DAW's context-aware Qt extraction with the equalizers' single-context extractor.

## Installed diagnostic candidate

Protected PR34 merged the tested tree without changing its bytes. Required hosted
Linux checks pass **58/58, 112.38 s**, and the Windows core cross-build passes;
this is not a native Windows runtime test.

The new local Ubuntu candidate is
`0.1.0~preview.20261008013908.97a307fcf2ba`. All **710** tracked corresponding-source
files were independently compared byte-for-byte. A normal package-manager upgrade
in the owned Ubuntu rootfs replaced the earlier candidate without installing a
compiler/SDK or using a downgrade override. The installed executable matches the
DEB's stripped executable hash.

The actual installed GUI selected the private audiotestsrc mono port, armed and
recorded with monitoring off. Its visible explanation reports clock29 position0,
block1024, engine frame1024 and previous clock29 position0/block1024. The saved
project attaches **1,024** verified raw frames; the finalized journal preserves
end reason6, zero rejected frames and the timing origin. WAV and PCM hashes,
asset/clip/journal agreement and explicit route intent were independently checked.
Save and normal Quit completed; the launcher exited0 and all owned processes
retired. The control helper's exit137 followed PID-namespace teardown, not an
application crash. Private recording nodes/links retired, and host default
metadata and existing links stayed identical.

This is a new diagnostic-failure workflow, not a repeat of sustained successful
recording/EQ/export on the new binary. The earlier candidate's successful workflow
evidence retains its original scope. Private Xvfb/PipeWire and a shared-kernel
read-only rootfs do not qualify a complete desktop, physical audio or real-time
deadlines. Reopening still does not restore the detailed session fault receipt;
the portable sidecar and native startup/alignment policy remain next.

The [separate installed receipt](../tests/results/X007/2026-10-08-installed-recording-fault.json)
and its **2,086,164-byte** capsule retain 39 logical entries, verified CRC and every
selected byte, including visible GUI screenshots and the preserved take. Original
receipts and takes were not rewritten. The candidate/source pair remains local;
no binary release was uploaded.
