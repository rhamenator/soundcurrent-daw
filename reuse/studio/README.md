# Audited GPL DSP reuse

`upstream/` retains unmodified Studio `src/dsp.cpp` and `src/dsp.h` from commit `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b`. They are reference snapshots, **not compiled**. Exact source hashes, notices and adaptation details are in `provenance.json`. The repository GPL-3.0-only license applies; original SPDX notices are retained. No additional file-level copyright declaration existed in these files.

The DAW copies only the peaking-coefficient mathematics and biquad recurrence into `src/eq_coefficients.cpp` and `src/eq.cpp`, marked as modified. It adds prepared planar state, stable ID mapping, sample-timed events and smoothing. No absolute source-checkout path or network fetch is needed for the root build. The existing Studio/EQ repositories remain unchanged.

Candidate improvements for later upstream adoption are explicit output/headroom policy and prepared event/ramp transport. They require independent equalizer integration tests; the DAW does not change the equalizer behavior automatically.

2026-10-06: [current input review](../upstream-review.json) confirms that the three
reused coefficient/response/biquad functions have not changed. New equalizer prepared
profile/enhancement APIs do not replace the DAW's prepared engine. Original reuse
origins stay pinned; changed source context is retained separately. The read-only
`tools/check_equalizer_reuse.py` detects later changes to reviewed inputs.
