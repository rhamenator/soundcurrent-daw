# Selection publication failures and interruption

This M2 acceptance step builds on [independent bounded publication](165-bounded-selection-publication.md).
It exercises the production `persistStretchSelection` path with owned Unicode
project/raw-audio fixtures. It does not change the file format or production DSP.

## Linux acceptance

The final focused CTest passes 351 parent-process assertions in 0.10 seconds.
Each case checks the saved project bytes and decoded Session, raw WAV bytes,
older selection/lock bytes, resource reservations, native descriptor count and
availability of the publication lease. Recovery inspection and fresh scanning
preserve the interrupted files; only the fixture owner cleans them afterward.

| Boundary | Evidence and expected result |
| --- | --- |
| Write after 17 bytes | Test executable's linker wrapper returns ENOSPC; no success digest, final leaf or leftover temporary file. This is injected disk-full handling, not a full filesystem. |
| Zero-byte write | Explicit I/O refusal and owned temporary cleanup. |
| File flush | Injected EIO before publication; final leaf absent and temporary cleaned. |
| No-overwrite link | Injected EIO; existing leaves unchanged and temporary cleaned. |
| Temporary-alias unlink | Injected EIO after successful link; complete final leaf remains, temporary cleanup succeeds, recovery reports Incomplete without a verified render. |
| Directory flush | Injected EIO after final publication; complete leaf remains, no success digest, recovery reports Incomplete without attaching an edit. |
| Short writes and EINTR | Actual writes limited to 17 bytes, one injected EINTR; retry completes and returned digest equals an independent final-file hash. |
| Cancellation after file flush | Stop requested after actual successful file flush; the cancellation poll refuses publication and cleans the temporary. |
| Kernel file-size refusal | Owned child sets its own RLIMIT_FSIZE to 128 bytes and ignores SIGXFSZ; actual write returns EFBIG, cleanup succeeds, parent limits remain unchanged. |
| Process interruption | Four owned children actually receive SIGKILL after write, file flush, link and alias unlink; parent reaps each exact PID and checks the resulting file set. |

Before the final link, killed writers leave only an owned temporary remnant.
After link but before alias unlink, both names refer to the same inode, verified
with native `stat`. After alias unlink, only the final manifest remains. Fresh
inventory excludes temporary names; a final manifest without helper output is
Incomplete, while an operation without a final manifest is Invalid. None is a
verified render or an adopted Session edit. The killed child's kernel releases
its writer lease; the parent confirms a new exclusive lease is available.

Fault interception is confined to this test executable and this exact fixture's
selection paths/descriptors. Production targets contain no injected hooks. No
audio route, VM, host disk-filling exercise or system-wide resource limit is used.

## Evidence and limits

[Retained evidence](../tests/results/M2/2026-10-10-publication-faults/README.md)
binds the final test to source commit `a84e09b` and all 310 first-party C/C++/header/
CMake inputs. Original setup failure and the earlier run are retained separately;
the earlier uncommitted prototype is not qualified as the final source.

The common healthy/duplicate-preservation fixture builds on Windows; Linux-only
linker interception, fork, EFBIG and SIGKILL cases do not establish Windows fault
behavior. Native Windows storage failure and interruption, installed publication,
actual filesystem exhaustion, power loss, concurrent throughput and processing
quality remain open. The 351 count excludes assertions made only inside the
owned child. Full M2–M11/X004/X005, European language qualification and separate
functional/quality/content/native-project goals remain incomplete.

## Next

Finish protected paging/publication reviews and current-head platform gates.
Qualify Windows publication failures using owned native file handles, then
continue M2 editing and processing-quality acceptance.
