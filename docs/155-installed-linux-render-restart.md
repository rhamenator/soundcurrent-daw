# Installed Linux retained-render restart qualification

Status: normal installed restart workflows qualified for one owned stereo
fixture on Ubuntu 26.04 amd64. The full studio goal remains active and incomplete.

Implementation source: `5300010ab521917027501c831b550092e535c210`, tree
`83ff01f11fa64c32c424af8d5ed36091dfdbd37c` ([checkpoint154](154-render-restart-review.md)).
Local preview: `0.1.0~preview.20261010165009.5300010ab521`.

## Installed workflow

The package manager upgraded the previous `083aefed3afd` preview inside a
disposable Ubuntu overlay. Runtime desktop dependencies were present; C++
compilers, CMake and Qt development tools were absent. Only installation ran as
root. Five sequential normal installed app processes and the observed installed
sibling helper ran as UID 1000, with no effective capabilities and
`NoNewPrivs=1`. Xvfb, D-Bus, PipeWire, mounts, process IDs and networking were
isolated; `/dev/snd` was absent. No VM or host package installation was needed.

| Process | Actual actions and observations |
| --- | --- |
| 1 | Open Unicode project/media names. Render duration 3/2 with protected marker 4096 → 6144; observe the installed helper while live. Saved project unchanged. Close dialog and normal Quit without Apply. |
| 2 | Fresh process opens the same project. Scan finds Ready; explicit Review restores exact controls/marker. Changing output to 6145 disables Apply/Prepare; restoring 6144 enables them. Prepare then Stop, Apply/Save, Undo/Save, Redo/Save, WAV export, normal Quit. |
| 3 | Reopen applied project. Scan shows Already attached and disables selected Review. Repeat WAV export, normal Quit. |
| 4 | Original target with start frame changed by one. Scan shows Target changed or missing and disables selected Review. Normal Quit; no project write. |
| 5 | Original target restored; parent duration request changed to 499999/333333 with original separate binding retained. Scan shows Invalid or missing files and disables selected Review. Normal Quit; no project write. Preserve both manifests, then restore original bytes. |

All five actual waits returned zero. Preparing audition registered one node in
the owned virtual graph; Stop removed it. Every Quit left the original four
virtual nodes. Play audition was not selected; audible monitoring and physical
playback remain unqualified. Screenshot status/button observations are manual
visual inspections, not automated Qt property assertions.

## Independent media, state and package checks

The source has 32,768 stereo float frames at 48 kHz with peak 1.5. Rendering
produced 49,152 frames. A separate RIFF/RF64 parser checks all finite samples,
format, length, whole-file/sample hashes and completion metadata. Raw bytes still
match the original asset hash. Both WAV exports match every rendered sample bit
and preserve the 1.5 peak; repeated export files also match byte for byte. The
2,304-frame protected unity span matches both raw channels exactly.

Undo restores the whole original project semantically; Redo restores the whole
applied project. Save/reopen leaves the applied saved bytes unchanged. The parent
selection's exact request, minimal original target and markers match the applied
anchor; original manifest bytes match `selection.sha256`. The altered manifest
does not. v5 helper metadata carries rounded duration/warp; the parent binding
preserves the exact rational controls.

The fresh native producer passed seven groups: worker/protected worker,
desktop controller, stretch/protected UI, localization and source integrity.
Scoped inputs remained unchanged. Installed helper hashes match prepared inputs.
The GUI is stripped during packaging, so its installed hash matches the packaged
hash, not the unstripped native build hash. No older receipt is reassigned.

The matching source archive independently verifies all 2,223 tracked blobs and
executable modes; PowerShell CRLF conversion follows Git attributes. A separate
full archive SHA-256 read matches the package receipt. Neither the installer nor
its corresponding source archive was uploaded.

## Evidence and remaining work

[Qualification](../tests/results/M2/2026-10-10-installed-render-restart/qualification.json)
binds 160 entries in `capture.zip`: original logs, commands/scripts, screenshots,
project/manifest snapshots, helper metadata, owned synthetic audio and
native/package/source receipts. No installer, executable, credentials or personal
recording is included. Audit with:

```sh
python3 tools/verify_installed_render_recovery.py
python3 tests/installed_render_recovery_evidence_tests.py
```

The audit checks bounded ZIP integrity, retained media/state and source/package
identity. Twenty-two positive/refusal cases include modified raw/export/selection/
binding, wrong helper/package/source identity, duplicate process IDs, failed Quit,
unexpected graph nodes, inconsistent asset ID/path/format, unrelated edits,
changed worker/protected qualifications and broader scope claims. These commands audit retained
evidence; they do not replay installation, GUI interaction or native audio.

This qualifies upgrade in an isolated Ubuntu runtime, not a fresh physical OS or
hardware listening test. Abrupt parent death, interrupted publication, power loss,
Windows installed restart/reparse/held-writer checks, scalable archival, processing
quality and European translation review remain open. The Windows installer for
`cceedcc6` remains bound to its older source.

Next: rebuild current Windows source and qualify installed normal restart/review
on the independent acceptance VM, one VM at a time; then continue M2 quality and
scalable recovery. M2–M11/X004/X005 and separate functional, quality, content and
native compatibility obligations remain intact.
