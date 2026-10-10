# Stretch context, local timing and grouped-channel observations

2026-10-10 UTC. Frozen SC-DAW-BASELINE-2026-10-05 is unchanged. This is a
bounded M2/M8 diagnostic checkpoint, not a new processor or a quality promotion.
[ADR104](decisions/104-local-warp-and-group-quality.md) defines the next gate.

## What actually ran

The [standalone C++ parent](../experiments/stretch-region-quality/probe.cpp)
prepares the protocol4 request, verifies ready before acknowledging start,
independently verifies the completed derivative, applies one structural edit,
checks exact Undo/Redo/save/reopen and compares the prepared live graph at blocks
37/127, split playback at53 and actual WAV export at41. Covered callback
allocation/free/blocking-lock counts are zero. This is synthetic graph processing;
no native audio device was opened.

The [runner](../experiments/stretch-region-quality/run.py) generated five original
48 kHz, 32,768-frame float sources: stereo impulses, harmonic attacks and two-tone
sustain, plus eight-channel impulses and attacks. Channel gains include inverted
polarity and input peak1.5; delayed copies use physical offsets0,3,…,21 frames.
Thirty-seven selected spans compare explicit context with26 matching whole-source
jobs. All **63 actual helper processes and226 actual parent processes** exited0
in24.445 seconds with1465 harness checks. The global deadline was240 seconds,
worker deadline10 seconds, parent timeout20 seconds, parent address-space
ceiling512 MiB and helper ceiling256 MiB. All processes were reaped.

Coverage includes integral/half-frame positions, odd/even context, real available
context at both asset edges, 1/1,3/2,1/4,4/1 duration and independent pitch0,
+7.00007,−24,+24 semitones across the selected cases. It is not the Cartesian
product of these settings, channel counts, musical material or sample rates.
Asset-edge fractional reads retain the existing positioned FIR's zero extension
of taps outside the asset. Explicit context never manufactures missing frames.

The production inputs remain unchanged from main **e73eef65**. Only this diagnostic
was compiled, linking content-checked cached libraries:287 include/source/vendor
input hashes match the qualified source. The helper executable hash is
`49073b25954d0d717909353b6cd18af92de172951216d4af5b48b167f7737c57`;
the final standalone parent hash is
`7e03cc399a2b675280ffc6824f27a291e9390120c5c569c7ef5e5fa3ab790031`.
Experimental sources were uncommitted when measured. Library/compiler/source
hashes, commands, requests, process outcomes and generated PCM are retained.
These hashes bind this observation; they are not a current installer receipt.

## Measurements and interpretation

All twelve unity crops match their positioned whole-source comparisons exactly.
Integral unity also matches original sample bits, including channel order,
polarity and float headroom. All25 non-unity pairs differ. The largest absolute
sample difference is **0.7454949319**, in fractional stereo sustain at3/2 and
+7.00007 semitones; maximum relative RMS difference is **1.6960392166**.
These differences establish dependence on processing region and history. The
whole-source render is a diagnostic comparator, not acoustic ground truth.

| Case | Input / settings | Maximum sample difference | Context channel0 peak / nominal landmark | Sign-corrected channel1→0 correlation lag |
| --- | --- | ---: | --- | ---: |
| 0 | Interior stereo impulse, unity | 0 | 64 /64 | +3 |
| 1 | Odd context, stereo impulse,3/2 | 0.4005360 | 143 /96 | −3 |
| 4 | Half-frame origin, stereo impulse,3/2 | 0.4015508 | 55 /95.25 | −3 |
| 21 | Even context, stereo impulse,3/2 | 0.4147286 | 144 /96 | −3 |
| 24 | First asset edge, stereo impulse,3/2 | 0.0000291020 | 63 /96 | +3 |
| 28 | Last asset edge, stereo impulse,3/2 | 0.3597771 | 135 /96 | −3 |
| 32 | Eight-channel impulse,3/2 | 0.3147183 | 60 /96 | +10 |
| 36 | Eight-channel harmonic attack,3/2 | 0.6563975 | 66 /96 | +3 |

The nominal landmark maps the known physical impulse/attack start into the
selected crop at n/d. A peak is the first largest absolute sample in that crop;
it is **not a universal event-time oracle**, especially after independent pitch
changes or when an attack extends beyond the visible selection. Correlations
remove each overlap's mean, require16 samples, search a bounded lag and prefer
the smallest absolute lag for numerical ties. Positive lag means the tested
channel is later; these chosen short windows can be ambiguous on periodic or
ringing material. Quantiles/centroid/peaks/correlation remain diagnostics.

Case32's channel-zero correlation lags are0,10,11,−11,3,9,9,36 frames; case36's
are0,3,6,9,12,15,18,21. The raw delayed-copy offsets, nominal map-scaled
displacements0,4.5,…,31.5 and measured waveform lags have different meanings.
The attack result does not establish universal group coherence; the impulse
result does not define one scalar phase-error score. The production helper uses
channels-together for mono/stereo and channels-apart for higher discrete counts.
An eight-channel render therefore does not qualify phase-coherent microphone
group warping.

Twenty-four central Hann-FFT observations on six whole-source sustain renders
have maximum interpolated carrier-frequency error **0.1577331764 cents**. This
supports the selected steady-tone frequency observations only. It does not
qualify vocal segmentation, formants, transients, perceptual quality or Q-PITCH.

## Retention and corrections

[The evidence folder](../tests/results/M2/2026-10-10-stretch-region-quality/README.md)
retains all five originals, all63 full processed WAVs, all37 exported crop pairs,
the six whole sustain exports, project/intent/completion state, all289 process
outcomes, scripts and logs in a9,673,174-byte archive. A standard-library
inspection passes **7233 checks**, independently parsing/hashing finite float
PCM, recomputing exact geometry/render keys, sample/RMS/energy/peak/correlation
metrics and a separate radix-2 FFT. It never replays the stretcher or a device.

The initial link failed because this host has the CMake-resolved libsndfile
runtime path but no `-lsndfile` development symlink. The diagnostic now uses the
actual cached CMake library path. The first run completed its helper but the
experimental parent incorrectly applied a media-only path validator to an export;
it refused before producing the comparison export. The corrected parent admits
only its fixed owned export names. The initial archive's8 MiB cap was too small
for all full WAVs; its refusal is retained and the cap is explicitly16 MiB.
No production fix or DSP rerun was required for that retention correction.
Earlier experimental source text is not reconstructed: failed-build source
hashes/logs are retained, while the executed successful sources are complete.

No VM, microphone/speaker route, installer, product binary upload or equalizer
change occurred. The earlier PR85 Linux109/Windows core41/Qt12 qualification
remains attached to its exact recorded sources, including Windows synthetic
merge49e59e52 and identical production tree5d8ed137. It does not qualify this
Linux-only diagnostic on Windows or a newer installed app.

## Next implementation task

Implement a bounded original **normalized warp-map planner and candidate probe**
outside the shipping app first. Use exact source/output domains and implicit
endpoints; compare the pinned R3/R2 key-frame adapters on multiple nonredundant
anchors, known attack envelopes, sustained phase probes and grouped delays.
Retain complete PCM, drain/warning/refusal outcomes and varied partitions.
Evaluate a pinned alternative only if the existing adapters cannot satisfy the
contract. Passing map arithmetic alone cannot enable the desktop marker editor.

Then implement one explicit movable raw-relative anchor, supervised rendering,
review/Apply, semantic Undo/reopen and identical live/export behavior, with
unsupported phase-preserving group modes refused rather than advertised. Full
P008/P011/P012, recording reliability, MIDI/mixing/plugins/launcher/monitoring,
content/import/spatial/video, recovery, native installation and all-European
localization remain required. Full F/Q/C/N parity and the active goal are incomplete.
