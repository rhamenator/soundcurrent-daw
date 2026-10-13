# Native and PipeWire panel failure diagnostics

Checkpoint 237 improves evidence for independent Windows recording failures.
It does not claim those failures are resolved. Full frozen-reference parity remains
active, including Linux/Windows, quality, content, interchange and European languages.

## Original independent runs

PR169 run 38329247072 at 338996c finished: Linux 154/154 in 441.16 s,
Windows native 81/81 in 229.35 s, and cross-build pass. Windows desktop passes 19/20
in 86.30 s, failing the existing Native device-selection finite-completion assertion.
The new PipeWire admission/main-window trace passes. Original terminal job logs,
APIs and selected Windows artifacts are retained. The failed Native panel project
was not uploaded, so its cause remains unproven.

PR170 run 38330442681 at 3c47867 has a distinct Windows desktop failure: 19/20 in
70.73 s, failing the PipeWire finite-result/export check before the Native selector
case. Its original desktop log/archive and text members are retained; its failure
cause is also unproven. The Windows collector self-test passes exact Unicode/audio
bytes and link refusal, but finds no preserved controller marker: the old panel
fixture still deleted its temporary project. Other original jobs remain live at the
retained observation. A later local pass does not resolve either original failure.

## Joined result diagnostics and retained projects

The Native selector test retains the original completion assertion. On failure it
prints each factory/prepared/query condition and exact joined result facts:
disposition, raw frames, capture/render outcomes, original ends, rendered consumption,
startup, processing/drift/buffer/fanout faults, owner/storage/recovery errors, wait
failure, SDK stage and packet/write/join counts. The same bounded text is saved in
the generated project. PipeWire panel failures report their separately typed result,
clock/receipt/startup/terminal and owner-failure facts; no backend clock conversion
or invented Native result occurs. Logging and file writes are test/control work.

A generated `sc-native-panel-é-/UUID` root replaces automatic temporary-directory
cleanup. Success removes it; exceptions retain it after stack unwinding closes
windows/controllers and joins owners. Its marker is emitted explicitly as UTF-8.
The CI collector accepts only the exact generated panel/controller marker and parent
policies, and separates their snapshot target names. Plain path/reparse/link checks,
three projects, 512 entries, 512 MiB per project, 16 MiB per log and 16 input logs
bound retention. Copies preserve and verify hashes through 64 KiB chunks. Manifests
explicitly disclaim atomic snapshots; this is cooperative joined fixture retention.

Local tests cover exact nested Unicode panel bytes plus controller audio bytes,
independent foreign/link/no-overwrite/UUID/count/log/byte refusals and empty results.
No queue sizes, deadlines, device timing, completion criteria or production behavior
were changed. No application UI source changed; localization inventory remains current.

## Qualification and next work

Current source 2ab295e binds 839 selected inputs. Fresh 199-step Release Qt build and
an earlier diagnostic panel run pass in 13.24 s; the final additional PipeWire
failure-report rebuild/run passes in 13.25 s with 118 checks. Existing
mixer/playback warnings remain retained. Synthetic SDK panel tests are not physical
or native Windows qualification.

PR171 original run 38330869925 at 6428369 is live with first diagnostic/retention
changes; preserve its original terminal evidence before updating that PR. Additional
local end/PipeWire diagnostics need a separately bound follow-up or later PR update.
Next diagnose retained original failures from actual project bytes and joined facts,
then qualify native playback of admitted takes, recovery, alignment and hotplug.
