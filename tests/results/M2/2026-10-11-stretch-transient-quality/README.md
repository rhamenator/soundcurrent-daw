# Original transient bank: retained audio and failed gates

Run `python3 tests/results/M2/2026-10-11-stretch-transient-quality/verify.py`
from a repository checkout containing source revision
`d54dd878644b1075fb88b2f0eaa5609c4e39c775` and original build revision
`20a062dbf8449cd6e6c2b167b2ed12a3320e46f1`. Source backup refs preserve both.
The verifier uses Python's standard library; no NumPy, audio hardware, worker
executable, installation or VM is required. It does not rerender the processor.

The 16,510,292-byte ZIP contains 127 bounded members, 54,892,297 bytes expanded:
all nine original and 36 rendered stereo float32 WAVs, original JSON outcomes,
worker intent/completion records, process observations, run log, prior build
inputs and configure/build logs. Signals are original synthetic GPL-3.0-only
fixtures. No private recording, executable or installer is included. The archive
and each member have byte counts and SHA-256 hashes; extraction is unnecessary.

The verifier independently parses RIFF/RF64 PCM and recomputes causal 32-frame
RMS, background admission, threshold crossings, exact rational timing error,
physical arrival differences and pre-event/event energy using direct sums.
It imports neither the original detector nor the original parsing helper.
Source and unity calibrations are checked against the recorded measurements.
Floating RMS/energy comparisons allow 1e-12 numerical rounding; exact onset
frames, rational drift and the strictly less than 1 ms gate receive no tolerance.
It binds 309 original Git inputs and original runner/contract/helper/observer
hashes. Build logs and executable hashes document the reused original build;
this is not an independent rebuild or Windows execution claim.

All 36 isolated renders completed with exact duration and finite PCM. There are
216 channel/event observations: 56 resolved (52 timing passes, **four failures**)
and **160 unresolved**, which never count as passes. Maximum resolved drift is
1.564625850340136 ms. Silent-family pre-echo has **36 passes and 108 failures**
at the preregistered −40 dB threshold. The 72 background-bed energy observations
are diagnostic only. All nine unity renders reproduce source PCM exactly.

Successful audit means these retained results were reproduced, including their
failures. It does not mean Q-STRETCH passed. Listening, general/music onset,
protected/group phase, editable markers and tempo workflows, native Windows,
installation and full parity remain open. See [checkpoint169](../../../../docs/169-stretch-transient-quality.md).
