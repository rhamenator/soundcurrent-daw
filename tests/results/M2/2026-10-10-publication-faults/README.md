# Retained Linux publication-fault acceptance

Source: `a84e09bb90e2217b543f3774b42514cde9094f43`.
The final focused CTest reports 351 parent-process assertions. Six injected I/O
errors, retry of short writes/EINTR, cancellation after actual file flush, an
actual kernel EFBIG refusal and four actual SIGKILL boundaries are distinguished
in the original final log. See [scope](../../../../docs/166-selection-publication-faults.md).

`capture.zip` contains seven bounded text/JSON members: original missing-target
setup failure, configure output, first build/test, final build/test and source
bindings. The first run used an uncommitted prototype, had duplicated buffered
output after fork, and predates cancellation/descriptor assertions. It is kept
as an original observation, **not** attributed to the final source commit.
The final run has distinct single event lines and 351 checks.

`python3 tests/results/M2/2026-10-10-publication-faults/verify.py` audits original
member bytes, exact frozen Git source paths/blobs/hashes, final event counts and
declared limits. It executes no archive code, child process, GUI or audio.
No binary, installer, credentials or VM image is included. Original `.cache`
build/logs and test binary stay local. Linux test interception does not qualify
Windows faults, physical disk exhaustion, power loss or processing quality.
