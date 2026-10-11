# Frozen desktop retained-take acceptance

Six text/JSON members capture a source-bound incremental Linux Debug build and
actual eight-suite run. Executable hashes are recorded; executables and generated
PCM are not included. `verify.py` checks frozen Git inputs, archive/member hashes,
reported outcomes and scope. It does not execute the tests or independently
recompute audio. The C++ workflow uses actual owned synthetic capture and an
independent captured-frame oracle for live/offline output.

Two two-track groups are recorded per retained workflow. The 131-lane dropdown
projection is a separate alias-state fixture, not 131 recording runs. The focused
workflow runs once directly and once within the existing manual suite; exact
assertion counts are retained rather than treated as platform constants.

Use `python3 tests/results/M2/2026-10-11-desktop-retained-takes/verify.py` from a
checkout retaining the recorded source commit. See [checkpoint171](../../../../docs/171-desktop-retained-takes.md)
for controls, failure cases and remaining loop/crossfade/native/installed gaps.
No product binary, installer or release upload, physical endpoint or VM is used.
