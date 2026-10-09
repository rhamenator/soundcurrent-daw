# Original REAPER native-writer corpus

Writer envelope: **7.82/linux-x86_64**, unmodified official evaluation runtime.
This is an import investigation corpus, not a supported-version announcement.
Reference A 6.1.3/Reference B 15.0.30 and the DAW parity baseline remain unchanged.

The seven `.rpp` files were actually saved and reopened through public ReaScript
APIs. Their exact hashes/bytes are frozen in `manifest.json`. The observed JSON
records contain native API readbacks both before save and after reopen; those
properties agree. New runs generate new GUIDs/timestamps and need not be byte
identical. Regeneration compares the recorded properties after excluding new
track GUIDs, rather than rewriting the frozen fixtures.
Scoped Git attributes preserve the original RPP CRLF bytes. Only these two
original corpus WAVs are excepted from the recording-file ignore rule.

| Case | Authored properties / future conversion coverage |
|---|---|
| empty | Explicit48k project, no tracks |
| unicode-mono | Unicode track/take names, mono source, one audio item |
| offset-fades | Two items, nonzero positions/source offsets, explicit fade durations, quoted names |
| stereo-gain-pan | Stereo source, track/item/take gain and two pan layers |
| rate-pitch | Nonunity rate and pitch; processing equivalence unqualified |
| opaque-state-routing | Two tracks, send, markup-shaped name and opaque project extension state |
| midi-tempo | Two original notes and a tempo/time-signature marker; destination MIDI/tempo mapping pending |

The WAVs are original deterministic PCM authored by `generate_media.py`, not
recordings, vendor samples or rendered proprietary effects. Native default
project configuration and original authored settings are retained. No runtime,
vendor instruments/presets/themes/fonts, plugin implementation or documentation
is redistributed. The original test scripts, authored projects, observations
and media use this repository's **GPL-3.0-only** license. REAPER remains a separate
proprietary tool; valid licensing/evaluation is required for new authoring.
The official [download/evaluation page](https://www.reaper.fm/download.php) and
[public API reference](https://www.reaper.fm/sdk/reascript/reascripthelp.html) are
research references, not adopted source. The privately inspected runtime EULA,
archive/executable and libSwell hashes are identified in the manifest; archive
hashes were measured after HTTPS download and are not vendor signature evidence.

## Inspection acceptance

`reaper_native_corpus_tests.py` runs the real C++ worker against temporary Unicode
paths with **no referenced media copied**. It checks exact hashes/full byte
coverage, child PID, unverified statuses, unchanged source, original PCM and
TRACK/ITEM block counts against actual native observations. This does not prove
property mapping, native DSP/render equality, plugin compatibility, a Windows
source writer, parent Qt/bundle workflows or the whole RPP grammar.

CI needs no REAPER install, account or proprietary binary. It reads only these
frozen original files. Existing malformed-input/cancellation/refusal tests remain
separate from this valid-project corpus.

## Optional new authoring

On Linux, provide the separately acquired, hash-matching official runtime within
a valid license/evaluation period, plus a **new** lab path:

```sh
python3 tests/fixtures/reaper-7.82/author_in_lab.py /path/to/REAPER /path/to/new-lab
```

The driver checks runtime hashes and creates a private lab. Bubblewrap isolates
mount/PID/network namespaces, exposes only read-only runtime/system libraries and
the writable lab, clears inherited environment, and provides no display, host
audio devices or audio-server sockets. The stock writer can run these APIs with
no display connection. The owned supervisor waits at most20 seconds for the
complete receipt, then terminates/joins its exact child; a normal GUI exit is not
claimed. Individual files are capped at32MiB. The initial8MiB cap failed during
default-theme extraction (the completed theme is25,136,322 bytes); that failed
lab/receipt was retained privately and the32MiB reproduction passed.

Do not copy the lab's private profile/theme/effect caches into this repository.
Refresh frozen files only as an explicit corpus revision with new provenance and
acceptance. Windows-generated projects, renders, corrupt/future-version corpus,
complex takes/automation/plugins/routing and native import conversion remain
required follow-up work.
