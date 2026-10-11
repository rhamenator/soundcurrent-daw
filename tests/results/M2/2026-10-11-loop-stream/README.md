# Finite looped playback and recorded-pass evidence

Frozen implementation: `c6a4fa2e6a9ce143792e3440af90db0a135e4a5b`.
The source backup branch preserves this original commit. The capture contains
five text/JSON members, including 401 source-file SHA-256 values, configure/build
logs, CTest output and LastTest output. Executables are represented by hashes;
no product binary, installer, recorded PCM or release artifact is uploaded.

The actual incremental Linux Debug build uses two jobs, nice10 and ionice2:7.
All 12 selected suites pass. The focused test records five actual two-input passes,
checks seven/29-frame latency, raw sample values and monotonic device origins,
retains/comp-selects takes and saves/reopens. Playback/export, mixed physical-rate
fractional clips, overlapping fades, continuous EQ, inside-block wraps and
stop/cancel checkpoints are covered. See [checkpoint172](../../../../docs/172-loop-playback-recording.md)
for exact scope and open product/quality/platform requirements.

`python3 verify.py` validates bounded packet checksums, manifest shape and captured
log outcomes using only the standard library. It does not independently fetch or
authenticate source bytes, replay audio/code, or qualify a newer head. To compare
source bytes explicitly, use `python3 verify.py --source-dir /path/to/frozen/checkout`
with a checkout of the original implementation commit. It has no dependency on
historical Git objects being available in a minimal or squashed checkout.

Desktop loop controls, indefinite slot replenishment, native Linux/Windows loop
recording, physical hardware, installed preview, comp crossfades and full M2 or
frozen parity remain open. Existing stretch-quality failures are unchanged.
