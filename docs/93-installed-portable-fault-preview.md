# Installed portable-error preview

Ubuntu 26.04 amd64 local candidate:
`0.1.0~preview.20261008034000.743392ece10a`.

- DEB: `soundcurrent-daw_0.1.0~preview.20261008034000.743392ece10a_amd64.deb`
- SHA-256: `c2487d8be6474a0b6258537b7cbbdc81c18d42f432a703b9fcdf2d0572672c6f`
- Source: `soundcurrent-daw-0.1.0~preview.20261008034000.743392ece10a-source.tar.gz`
- Source SHA-256: `41aa9f81a0dc04eebe1357ed60fd2b386af84958bfff0f43a042ad135705c5b1`
- Exact [corresponding source](https://github.com/rhamenator/soundcurrent-daw/tree/743392ece10afa4b27a0378d5f5961dad059c500): **719** tracked files independently compared byte for byte.

Artifacts, `INSTALL.md` and `SHA256SUMS` are local in
`.cache/preview-portable-faults-ubuntu-26.04/`. No GitHub binary release upload.
Close the application, then from the artifact directory:

```sh
sha256sum -c SHA256SUMS
sudo apt install ./soundcurrent-daw_0.1.0~preview.20261008034000.743392ece10a_amd64.deb
```

The package includes an Applications entry/icon, declares runtime dependencies,
and uses existing PipeWire. Normal user launch needs no compiler or Qt SDK.
Complete desktop/menu qualification remains open. See the
[workflow guide](90-preview-guide.md) for recording, playback/EQ, project and WAV
export steps, and [portable error semantics](92-portable-recording-faults.md).

## Actual installed workflow

Normal package-manager upgrade from the prior `97a` candidate passes in the owned
Ubuntu Base rootfs, with no new runtime package or downgrade override. The
installed executable matches the stripped DEB payload. As UID/GID1000 with
effective capabilities zero, the actual installed GUI explicitly selects private
`sc-preview-source/capture_MONO`, arms and records with monitoring off.

The reproduced clock29 cycle1→2 position0→0 mismatch preserves a verified
**1,024-frame** raw take. Its asset, clip, journal and schema1.0 sidecar agree;
end reason6 and zero rejected frames remain intact. Save and normal Quit complete.
A new application process reopens the project and offers saved error details for
the already attached take. The review dialog displays both clock observations
and the precise position reason. Review audio is disabled; clicking it does not
start playback. Cancel/normal Quit complete, with both app exits and launcher
exit0. Reopen/review preserve the project, WAV, journal and sidecar byte for byte.
No recording endpoint or link is activated during reopened review. Owned nodes/
links retire; redacted host default/link fingerprints match before and after.

The [installed receipt and capsule](../tests/results/X007/2026-10-08-installed-portable-faults.json)
retain all eight actual action commands/exits, the exact launcher and private
scripts, screenshots, original take/state, byte comparisons, package/source
metadata, installed executable and redacted route inputs. ZIP CRC and all **82**
logical entries are verified; the archive is **2,304,204 bytes**. Four original
setup/status failures are separately retained: namespace cwd differs from host
cwd, and a pending-check wrapper returns8. Matching owned-file inode lookup
resolved the helper selection; no pre-fix application failure is asserted.

## Regression and remaining work

Latest production inputs pass Linux Debug **63/63, 179.68 s** and seven affected
ASan/UBSan/LSan checks **7/7, 31.02 s**. Initial required hosted Linux passes
**59/59, 110.37 s**; Windows passes its core cross-build only. Hosted checks for
subsequent test/documentation commits are separate.

A review suspected normal Stop could create a spurious fault-storage warning.
The existing `AudioBridge::retainFault` guard excludes `Stopped` before publishing
readiness. [Additional assertions](../tests/results/X007/2026-10-08-portable-fault-normal-stop.json)
exercise prepared/running Stop, completed ranges and the controller's normal
manual-stop take. They prove there is no first fault, sidecar or storage warning:
Debug **2/2, 1.45 s**, affected sanitizer **2/2, 3.69 s**. Production inputs and
the packaged executable are unchanged; the exact package source remains `743392e`.

This fault/reopen workflow has its own scope. A subsequent [normal installed
workflow on the same binary](94-installed-normal-preview.md) now qualifies an
11.264-second recording, live EQ/Undo, reopen and independently checked export.
The earlier `85ec` candidate retains its own evidence. Private Xvfb/PipeWire on a shared kernel do not
qualify a complete desktop, physical audio, real-time deadlines or native Windows.
Original native71 and 2,048 leading zero frames remain unresolved; no valid
silence is trimmed. Multi-track/manual-punch durable diagnostics, detailed writer
exception persistence, broader setup/recovery and language qualification remain
required. No full frozen F/Q/C/N family or X004/X005/X006/Europe gate is promoted.

Next concrete task: distinguish native missing-buffer substitutions from valid
input silence and choose a tested acquisition/alignment policy; then complete
desktop/native Windows previews.
