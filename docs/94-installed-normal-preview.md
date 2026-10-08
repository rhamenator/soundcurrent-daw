# Current candidate: normal installed recording workflow

The same Ubuntu 26.04 amd64 candidate,
`0.1.0~preview.20261008034000.743392ece10a`, now passes a separate normal installed
recording/live EQ/Undo/Save/Quit/reopen/WAV-export workflow. Its package and exact
719-file corresponding source are unchanged. See [package hashes and installation](93-installed-portable-fault-preview.md).
The owner has a byte-verified copy of the DEB/source, `INSTALL.md` and
`SHA256SUMS` in `Downloads/SoundCurrent-DAW-Preview-2026-10-08/`.

## Evidence

The actual stripped installed executable runs as UID/GID 1000, capabilities zero,
on the owned read-only Ubuntu Base rootfs, with private Xvfb/PipeWire and no
physical audio device. An explicitly owned PCM16 WAV player feeds the mono input;
playback goes to a private silent sink. Host defaults/existing links are unchanged.

| Workflow | Result |
|---|---|
| Normal recording and Stop | **540,672 frames / 11.264 s**, 48 kHz mono float; verified attachment, end reason UserStop, zero rejected frames |
| Error behavior on healthy Stop | No first-fault sidecar or visible diagnostic-storage warning |
| Raw preservation | Original WAV/journal SHA-256 unchanged by EQ, Undo and reopening |
| Live EQ and Undo | Actual GUI −12→−6 dB→Undo; independently recorded output has **73** stable −12 dB windows and **22** stable −6 dB windows, in the expected order |
| Project workflow | Normal Save/Quit/reopen preserves project, media and settings byte for byte; Undo restores −12 dB |
| WAV export | Explicit frames 0–480,000: **10 s**; independent direct-form I peaking-EQ recurrence differs by at most **2.842170943040401e−14**, below the 1e−7 gate |
| Export after reopen | Second export is byte-identical to the first |
| Retirement | Both app exits and launcher exit 0; owned recording/playback nodes retire |

The owned output probe is deliberately interrupted with SIGINT and returns 1.
Its complete WAV header/data is independently verified; this is not an app crash.
The live level windows establish stable response, not exact event-frame timing
or real-time deadlines. The exported range is ten seconds of the 11.264-second take.

The [receipt and capsule](../tests/results/X007/2026-10-08-installed-normal-preview.json)
retain the exact launcher and all **nine** action/probe commands/exits, private
scripts/graphs/PNG, generated source WAV, original raw/journal/project, live output,
both exports, byte comparisons, independent verifier, installed binary and
redacted host comparison inputs. All **101** logical entries and ZIP CRC are
verified; the archive is **2,605,032 bytes**. Earlier receipts and takes remain
unchanged; this is a new workflow on the current installed binary.

## Qualification boundaries and next work

Protected PR36 merges the tested `c686f4017c784d74c168d82297df2b7415b14f39`
tree as `9c8df5cb6161fa43ab8d6081dec114e8f38bfb9a`. Final required hosted Linux
passes **59/59, 139.63 s**; Windows passes its core cross-build only. A verified
ZIP and restorable Git bundle back up that source/evidence; transfer to the
owner's Windows machine remains pending a destination. This does not establish
a native Windows installer/runtime. No binary release upload or host installation.

The first **2,048** raw frames are still zero. Every later sample exactly matches
the owned periodic source at a retained phase, but that source cannot independently
detect missing whole 48-frame periods. No samples were trimmed and no amplitude
validity gate was added. Original native71 and acquisition/alignment remain open.
Complete desktop/menu/Wayland/HiDPI, physical sustained recording/RT, native
Windows and broader installation/recovery qualification remain required.
Translation drafts and all full frozen F/Q/C/N, X004/X005/X006 and Europe gates
remain incomplete. This delivers a useful bounded Linux preview, not full parity.

Next: observe native input availability separately from sample amplitude, define
a tested acquisition/alignment policy, then complete desktop/native Windows
preview workflows. Keep original recordings while that work continues.
