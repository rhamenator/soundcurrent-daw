# Positioned clip playback and export

Date: 2026-10-09. **SC-DAW-BASELINE-2026-10-05** remains Bitwig6.1.3 and Cubase
Pro15.0.30. Functional/quality/content/native compatibility remain incomplete.

## Previous checkpoint

PR74 merged exact qualified `07e9935abab04331a99e35e403cccf665206af40` as
`cb16dfcdec4b1bf2ceec9ea7ca7a965fb485d235`, with identical tested/merged trees.
Run37938183942 passed unchanged protected contexts: Linux95, Windows native
core31, Qt8 and cross compilation. Three digest/CRC-verified log archives and
15 source/48 evidence hashes are bound by its final receipt. This qualification
applies to the preceding streaming component. It does not qualify this increment.

The corresponding local Ubuntu preview was refreshed at that source, version
`0.1.0~preview.20261009134438.07e9935abab0`; 11 selected local checks and paired
package/source preparation passed. No host install or binary upload was inferred.
The new positioned reader is not included in that package.

## Implemented workflow

Owned assets retain their physical sample rate and immutable raw bytes. A clip's
timeline start, length and fade anchors use project frames. Its source origin is
an integer frame plus an unsigned64 exact rational fraction. `SourceFrameMap`
checks carries, signed translation, extent and common-denominator overflow, even
beyond2^53. `CropClip` consumes signed project frames; `SplitClip` retains exact
fractional source positions. Integer source range edits refuse off-grid deltas.

Schema1.10 persists fractions and `soundcurrent.src-positioned-best-v1`.
Schema1.0–1.9 matching-rate state migrates neutrally; used mixed-rate old state
refuses ambiguous reinterpretation. Unknown processor versions, malformed types,
fractions/denominators and source extents refuse. Undo/Redo and grouped validation
preserve atomic state. Independent musical tempo, playback-rate and pitch/stretch
parameters are not implemented by physical sample-rate conversion.

The immutable positioned FIR uses the pinned best-sinc table and adapted
recurrence from libsamplerate0.2.2. Original upstream source remains unchanged;
BSD-2-Clause notices, exact source hashes, compiled consumers and modifications
are retained. See [ADR097](decisions/097-positioned-clip-reader.md),
[notices](../THIRD-PARTY-NOTICES.md) and [manifest](../third_party/manifest.json).
No new dependency version, proprietary algorithm or equalizer checkout changed.

### Data flow and admission

`MediaReadCache` verifies the owned descriptor's hash and physical rate/layout/
extent and reads integer source windows. `TrackReader` fetches bounded complete
asset context, evaluates absolute positions into a prepared float slab, applies
project-domain clip fades/gain, then mixes overlaps. Both `MixReader` live playback
and offline export use this same path. Seek prepares a fresh reader generation;
there is no history reset, private upstream state ABI or source-prefix replay.

Equal physical rates with an integer origin copy exactly. Equal rates with a
fractional origin filter. Near-one fixed-point phases carry to the next source
center rather than dropping its center tap. Input/output storage must be disjoint;
finite source/output and complete context are checked. Worker failures publish no
partly prepared audio slab. Source asset boundaries zero extend; crop/split
boundaries do not truncate filter context. The exported clip interval excludes
residual samples outside its declared project range.

Per-reader admission charges binding metadata, its maximum source-context buffer,
one converted output slab when needed, and the existing sum/slab/cache grants.
Source buffers are reused across clips. Static coefficient code/data is not an
RSS measurement or per-reader grant. A short shared grant refuses before media
admission reads. Native callbacks retain bounded prepared-slab consumption; GUI,
disk work, allocations, exceptions, logging and locks remain off that callback.
This recorded-asset worker design is not a zero-latency live-input SRC/PDC claim.

### Desktop

The selected clip shows its exact fraction and source/project rates. A signed
project-frame crop control uses the current timeline start/length fields and
supports Undo/Redo. Split retains its exact source phase. The owned-media selector
includes matching-layout assets at different physical rates; insertion computes
their ceiling project duration. Source-file edits remain non-destructive.
These controls do not adopt foreign-suite rate/pitch/effect properties by name.

New contextual source strings flow through all34 draft catalogs. Empty/unfinished
entries use English fallback; no new native-language review, delivered-language
coverage or European completeness is inferred.

## Acceptance evidence and bounds

The first Release workflow executed753129 checks before the additional storage/
admission cases. Across121 rate pairs,323574 source-aligned output samples match
the pinned streaming converter exactly (maximum difference0). Nine layouts
1/2/3/4/6/8/32/128/256 preserve channel identity. A separate arbitrary1/7-origin
1kHz sine comparison has maximum error3.43304e-7; near-one fractions retain their
center tap. These points do not establish full Q-RESAMPLE or suite quality parity.

The independent Python `Fraction` oracle executes755 actual C++ mappings:
509 accepted and246 refused, including arbitrary denominators, checked LCM,
signed-limit translations, carries, underflow and frame overflow. It never uses
floating point or duplicates the C++ quotient/remainder implementation.

Twenty owned recorded-WAV projects cover four source/project pairs and layouts
1/2/8/32/256. Live/export, callback/slab partitions, fresh seek, split and crop
have exact waveform equality; finite headroom exceeds1. Raw hashes and original
Save/reopen state remain unchanged. Existing15 selected Release regression gates
passed in46.95s, including schema/edit/import-state/cache/mix/export behavior.
Final updated counts, desktop, sanitizer and exact-source Windows qualification
must be read from this checkpoint's recorded receipts, not inferred from this
initial pass. The separate8192-track sanitizer180s timeout remains open.

## Resource stewardship

No VM was started. The owner-authorized DAW disposable-clone cleanup reclaimed
118.457GiB while preserving originals, pristine template, development VM and
recovery copies. Private XML/NVRAM/cleanup receipts remain outside source.
During the Veeam reset/backup, builds use two jobs with low CPU/I/O priority;
large paired preview archives and installed VM tests are deferred.

## Remaining work / next useful preview

1. Qualify this exact source on native Windows core and Qt and retain raw CI logs;
   cross compilation alone supplies no native runtime evidence.
2. Refresh paired Linux/Windows source and installers after disk pressure clears,
   including the newly compiled BSD kernel notices; installed/native playback at
   mixed source rates is a separate acceptance workflow.
3. Implement explicit fixed playback rate, then independent pitch/stretch with
   latency/tails, resource, quality and safe graph replacement decisions. Tempo
   mapping/rate automation, reference-aligned renders and actual listening remain.
4. Typed foreign property conversion/loss previews, adoption/Undo/reopen and
   originating-suite corpora remain required under X004; copied raw media and
   sample-rate conversion do not claim native-project compatibility.
5. Keep full M2–M11, Linux/Windows, X004–X007 and all-Europe work in scope. No
   milestone name or passing synthetic waveform promotes F/Q/C/N completion.

## Qualification organization and observed repair

The actual desktop inverse-crop assertion initially failed: a mathematically
restored1/7 source position was stored as21/147. New crop/split edits now reduce
stored fractions; Undo preserves the exact prior snapshot. The unchanged desktop
assertion then passed, including signed reveal, redo, insertion and save/reopen.

The first combined unoptimized ASan/UBSan workload hit90.17s. After removing
repeated array/span views from the tap loop, it hit90.06s; the complete kernel
comparisons had finished at46.242s and owned-WAV checks were progressing. These
failures are retained and do not qualify the aggregate command under90s.
Independent numerical/state and WAV integration acceptance families are now
registered separately, each with its90s process budget. They call the identical
full case sets; the default combined executable remains available for the full
Release run. No rate pair, layout, malformed state, sample equality, grant, raw
hash, undo/seek/crop case or assertion was removed. This is test process budgeting,
not a native audio deadline/throughput claim. Final family counts are recorded
separately; each process also checks its RT audit counters.

The unmodified coefficient table is now a SYSTEM include. Its intentional
double-literal-to-float conversions had produced about94MB of warnings per build.
Application warning flags, source-bank hash verification and sanitizer
instrumentation remain enabled. Binary installer inputs now require the pinned
BSD license text; Linux verifies missing/stale license refusal and normalized
file modes. No preview archive or system install is inferred from those tests.

## Final local source qualification

[Recorded receipt and raw logs](../tests/results/M2/2026-10-09-positioned-clips/)
bind578 production/test/dependency/catalog input hashes and six Release executable
hashes. The rebuilt desktop and three sibling helpers are included. Release
23/23 passes in43.85s. The unchanged default combined executable runs753174
checks in6.628s; the two registered families total753175 because each independently
performs its final RT audit assertion. Source-aligned323574 samples still match
the pinned converter exactly, with arbitrary-fraction sine error3.43304e-7.

Debug O0 ASan/UBSan7/7 passes in129.17s: kernel/state750959 checks in46.84s,
owned-WAV2216 checks with56.802s measured workflow time, independent755-case
oracle, desktop crop/processing, state and grouped edits. Each numerical/WAV
process keeps its90s budget. Leak detection is disabled. The failed combined
draft commands do not qualify aggregate execution under90s, and the older8192-track
sanitizer timeout remains open. Draft failure logs have no exact committed
source/binary qualification; successful current input hashes are explicit.

Linux package input refusals/modes, Windows deployment-input refusals and34-catalog
structural checks pass. The current source does not yet have native Windows CI
qualification, a new paired installer, system installation or native audio replay.
The previous95/31/8 log archives are separately retained and verified against
PR74's digest/CRC/count receipt; their scope remains that preceding source.
