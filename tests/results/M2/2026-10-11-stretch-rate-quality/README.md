# Compact Linux sample-rate quality observations

Original bank: source `20a062dbf8449cd6e6c2b167b2ed12a3320e46f1`.
Parent fixture correction/retry: `8058778a01cffe16b2c4a3ab252c6613fbf63ee5`.
See [contract and limits](../../../../docs/168-stretch-rate-quality.md).

`observations.zip` retains eleven text/JSON members: the preregistered contract,
309 source input hashes and executable hashes, original generated-source
calibration, every process outcome/request/receipt, every measurement, original
summary, configure/build/run logs, and the separate parent retry receipt/log.
The initial parent overwrite refusal remains a failure in the original bank.
It is not overwritten or retrospectively attributed to the corrected harness.

Full generated PCM remains local under `.cache/stretch-rate-quality` (about
55 MB including the retry). The compact public audit validates frozen Git
inputs, protocol/measurement arithmetic and the reported sequence; it **does
not recompute spectra or verify PCM from hashes alone**. Reproduce actual DSP
observations using the source runner and existing worker/parent verifier on an
owned folder. No binary, installer, personal audio or credential is retained.

`python3 tests/results/M2/2026-10-11-stretch-rate-quality/verify.py` performs only
the compact audit in a full clone. NumPy 2.3.5 was used by the local diagnostic;
it adds no shipping dependency. This is Linux steady-tone/duration/workflow
evidence, not full Q-STRETCH, Q-PITCH, Windows, listening or installed acceptance.
