# Admission sanitizers and controller failure retention

Checkpoint 236 qualifies the current take-admission path and improves evidence for
future failures. Full professional-suite, Windows and European-localization goals
remain active.

## ASan and UBSan

Source 88babdb binds 839 selected source/checkout inputs. The admission/engine/UI
implementation remains unchanged from f3bc0aa; the controller test's failure-path
marker now emits UTF-8 explicitly, and CI gains a bounded retention helper.

A fresh headless Debug ASan/UBSan build completed 145 steps, followed by two steps
for the marker correction. The controller acceptance workflow passes in 5.78 s:
record/inspect/admit, exact portable evidence, explicit placement, Save/Reopen,
Undo/Redo, copied Unicode root, original and processed export, and refusal cases.
ASan uses `detect_leaks=0:halt_on_error=1`; UBSan uses
`halt_on_error=1:print_stacktrace=1`. Leak detection is not qualified here.

The genuine private PipeWire 1.6.2 sanitizer fixture preserves 100,096 bit-checked
raw frames and delivers 8,192 processed/sink frames, with 392 capture and 412 render
receipts. It admits at explicit frame 257, saves all eight evidence references,
verifies media and reopens the canonical project. Audited callback allocation,
free and blocking-lock hits are zero. The owner-only dummy daemon and all owners
join; no physical/default-device/volume/user-playback or VM changes occur.

The old `mix_reader.cpp` unused-variable warning remains in the log. This evidence
does not establish Qt, installed-package, hardware or native Windows qualification.
Fresh TSan build (132 steps) and controller acceptance pass in 5.88 s, without
suppressions. Its simulated native capture/render roles run alongside the actual
controller, processing and disk workers. Qt/opaque SDK internals are outside this
TSan scope.

## Bounded CI failure snapshots

The previous Linux controller failure had logs but no failed take files. CI now
runs a collector on Linux and both Windows runtime jobs before artifact upload.
Only generated `sc-pw-controller-é-/UUID` roots from explicit CTest failure markers
are accepted. Limits are three projects, 512 entries and 512 MiB per project,
16 MiB per log; linked/reparse or nonplain paths and existing snapshots are refused.
Copies use 64 KiB chunks and verify before/after/copied SHA-256 and lengths.
The manifest explicitly reports that this is not an atomic live-project snapshot.

Local tests qualify exact Unicode/audio bytes, foreign paths, links, overwrite
refusal, invalid UUIDs, project-count/log/byte bounds and no-marker behavior.
Windows executes the same tests independently in its future CI run. These local
Linux tests do not qualify Windows links or runtime behavior. Failed take binaries
may be retained in CI diagnostic artifacts; public source evidence packets contain
only text, logs, configuration, metadata and hashes.

## Independent CI and remaining work

Original PR169 run 38329247072 at 338996c remains live at the current observation.
The Windows cross-build passes. Desktop passes 19/20 in 86.30 s: the new
PipeWire main-window admission trace passes, but the existing Native device-selector
finite-completion assertion fails. Its original job log/archive and 20 text members
are retained; the cause remains unproven. Linux and Windows native jobs are still
running. Preserve original terminal logs and artifacts before correcting failures;
never replace the original evidence with an unchanged retry.

Next complete current TSan and independent Windows qualification, then native
playback of admitted takes and remaining recovery/alignment/hotplug workflows.
Earlier clock, pool-pressure and owner failures remain causally unresolved.
