# Installed Linux GUI interruption and fresh-process recovery

2026-10-10 UTC. One actual installed partial-render interruption is qualified in
an isolated Ubuntu 26.04 amd64 runtime. The full goal remains active/incomplete.
This extends [checkpoint160](160-linux-parent-interruption.md); the earlier
controller subprocess is not substituted for an installed GUI.

## Exact preview and bounded setup

The package is the existing local `0.1.0~preview.20261010165009.5300010ab521`,
source `5300010ab521917027501c831b550092e535c210`, tree
`83ff01f11fa64c32c424af8d5ed36091dfdbd37c`. Its installed stripped GUI and sibling
worker hashes match the original [package qualification](155-installed-linux-render-restart.md).
A fresh byte comparison of 184 tracked `src`, `include`, `ui` and stretch-worker
inputs matches that source against current `b525b81`. This is not a new compilation,
new release, or reassignment of old binary receipts to a later commit.

Installation runs as root in a disposable volatile overlay. Installed app,
helper and test supervisor run as UID1000 with CapEff0 and NoNewPrivs1. C++
compilers, CMake and Qt development tools are absent. Private PID/mount/network/IPC
namespaces, Xvfb, D-Bus and owned virtual PipeWire nodes keep the test separate from
host playback; `/dev/snd` is absent. No VM is started and no host package is installed.
Python runtime packages support observation, not production DSP or development SDKs.

Two initial setup launches failed before opening the app. The first lacked Python;
the second attempted incompatible package ordering and tzdata replacement of
nspawn's bind-mounted localtime. Original scripts/install logs and actual container
exit1 receipts are retained. The successful fresh overlay installs minimal Python
in dependency order with timezone synchronization off. It does not relabel failed
attempts as completed GUI sessions or modify the base rootfs.

## Actual workflow

The owned fixture has Unicode paths and 1,048,576 stereo float frames at48kHz,
peak2.0. The installed GUI selects duration3/2, pitch0, formant preservation off,
no context and no protected markers. It starts the actual installed worker.

1. An external watcher verifies helper PID, executable, parent, command and
   `/proc` start ticks, opens a pidfd and pauses that helper with SIGSTOP after
   observing partial audio. The stopped witness has4120 bytes and no completion.
2. A separate pidfd action kills only the source GUI. Its actual parent wait
   returns -9. The supervisor opens a fresh installed GUI with the same project.
3. Its recovery list shows **Renderer still owns this job**, duration3/2, pitch0.
   Selecting that row leaves **Review selected result** disabled. The helper is
   still stopped with its lease and identical partial bytes; it was adopted by
   the test subreaper after parent death.
4. A separate pidfd action kills that exact helper, checking start ticks/image/
   command again. The subreaper's actual wait returns -9, wait status9.
5. **Scan again** in the same fresh GUI shows **Incomplete render**, duration3/2,
   pitch0. The selected row still has Review disabled. Partial bytes/hash match
   the original stopped witness; no final WAV or completion marker exists.
6. Close recovery and normal Quit. The fresh app exits0; the two owned services
   exit0 and the container exits0. All four original virtual node identities
   match before/after. Saved project and original raw bytes remain unchanged.

The statuses and disabled buttons were manually inspected in retained screenshots,
including selected rows. They are not automated widget-property assertions.
No Apply, audition preparation, Play, capture or export was requested. The partial
file is preserved evidence, not a qualified completed audio result.

Parent death and subsequent helper death are two separate actions. This does
not establish automatic helper containment, real power loss, installed GUI
pre-acknowledgement or completed-orphan behavior. Controller tests cover separate
boundaries; their evidence retains its own scope.

## Evidence, backup and next task

[Capture and bounded audit](../tests/results/M2/2026-10-10-installed-linux-interruption/README.md)
retain original logs/scripts/receipts, project/raw/partial data, source/package
bindings and screenshots. No executables, installers, credentials or personal
recordings are included. File audits execute no archived code or audio. The source-binding audit reads
trusted Git objects from both recorded commits and compares all184 paths, SHA-256
values and blob IDs. Ten semantic refusal cases include three source-inventory
corruptions after ZIP/member hashes are rebound.

PR101 ordinarily protected-merged as `b525b81c3cbe69cd8325fcaa6843df64913a14c7`.
All four required current-head jobs passed with exact test-merge checkout binding:
Linux117 tests, native Windows desktop15, native Windows packet47, and core
cross-build. The reviewed/tested/merged tree and all18 reviewed blobs match;
no branch policy change or administrative bypass.

Next: perform the corresponding actual installed Windows GUI interruption and
fresh recovery on the independent SDK-free acceptance clone, briefly and with
no other VM active. Then implement bounded resumable recovery inventories.
M2 processing quality, broader interruption boundaries, power-loss/scalability,
M2–M11/X004/X005, all-Europe translation/review/UI, content and native compatibility
remain open, with separate F/Q/C/N gates. Product uploads remain unauthorized.
