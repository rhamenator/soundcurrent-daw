# Current native Windows restart preview: open qualification gates

2026-10-10 UTC. The frozen reference baseline and full DAW goal remain unchanged.
This continues [installed Linux restart acceptance](155-installed-linux-render-restart.md).

## Observed native source

The independent Windows development clone built clean source
`f1f4dbc4307b5d65ed600068be6f2139e86bf26f`, tree
`e27f96712e8d35ce8621219481a4c211f0e6e22d`, with MSVC, Qt 6.12.0 and
the existing SDK. The actual build exited zero after 228.128 seconds.
Build processes used BelowNormal priority and at most two workers.
No audio endpoint was activated.

| Gate | Actual outcome | Qualification boundary |
| --- | --- | --- |
| Inspection, approved WAV probe, checked copy | Actual owned helper exits and binary hashes retained | Synthetic files; no physical audio |
| V4 stretch bank | Passed; receipt records 385 checks and 1,045 artifact verifier checks | Source and binary identity remain bound to `f1f4dbc` |
| Native desktop tests | Six groups passed in 19.17 seconds | Offscreen Qt, including restart controller/UI tests; no installed normal-window claim |
| Protected V5 bank | Failed at comparison-probe observation deadline | No complete qualification receipt; installer production remains blocked |
| Normal Windows window | Did not launch | Scheduled interactive task had never run; session query reports no users |
| Refreshed installation and restart | Not run | Earlier Windows installer remains bound to `cceedcc6` |

The six desktop groups were worker-observation-failures, desktop-stretch-ui,
desktop-protected-warp-ui, desktop-stretch-controller, desktop-localization and
protected-warp-package-inputs. Their LastTest log and terminal record are retained.

## Failed probe observations remain failed

The first V5 bank stopped when the experimental comparison probe's
`cancellation/uniform` case exceeded the original ten-second observation budget.
Here cancellation names an antiphase audio fixture. It is not a user Stop test.
An explicit subsequent process inventory found no probe or worker left running.

One separately retained diagnostic of that case used an exclusive persistent
output directory and file redirects. Its actual owned process, PID 3020, exited
zero in 4.095 seconds with no forced retirement. This diagnostic had a 45-second
observation limit. It does not qualify the ten-second bank or explain its failure.
One later full-bank attempt stopped at `impulse/identity` under the unchanged
ten-second budget. The final process inventory was empty. Neither attempt
produced a complete protected qualification report.

The comparison probe is experimental acceptance infrastructure, not an installed
payload. No evidence yet distinguishes process execution, pipe observation,
path handling or guest load as the cause. No DSP change or timeout relaxation
is justified by these observations.

The harness now observes this argv-only probe with the same bounded concurrent
observer used for the actual workers. The budget remains ten seconds, with a
four-MiB capture limit. Failed observations include the exact family/profile,
owned PID, elapsed time, actual terminal exit, captured output and uncertain
retirement. An incomplete observation still fails the bank. All 18 comparison
cases and the complete PCM comparisons remain required.

Seven local process-observer tests pass, including an argv-only Unicode request
with stdin EOF, a full stderr pipe and an actual nonzero terminal exit. The
updated local protected bank passes 515 checks, 21 renders and all 18 complete
PCM comparisons, plus two parent/live/export/reopen checks. Existing Linux
binaries were reused because no C++ input changed. This is local harness
acceptance, not native Windows qualification or a new package receipt.

## Input verification and retained evidence

The initial input guard attempts refused before launching helper children.
The corrected scope checks 878 active files byte for byte, plus one explicit
Git export transformation for `.github/scripts/prepare-windows-desktop.ps1`:
the host working copy is CRLF, the native checkout and Git blob are LF.
Normalized bytes match; this bootstrap script is not a compiled app input.
Seventeen archived historical experiment programs are outside the selected
compiler/live-test scope. They are not silently counted as verified inputs.
These checks are not a complete corresponding-source archive audit.

[Retained evidence](../tests/results/M2/2026-10-10-native-restart-blocked/README.md)
includes original guard refusals, native records, immutable failed runs,
diagnostic WAVs, source scopes, local test logs and the final VM state.
The collection script's first session query failed; its later attempt had a
script typo and stored null session fields. Both logs are retained. The separate
actual quser process exited one and reported no users; no null field is treated
as a successful session check.

The owned scheduled task was removed after confirming it was not running.
The development clone shut down normally; all VMs are off. Originals and the
pristine template were untouched. No installer, runtime binary or release was
uploaded. Equalizer repositories were not changed.

## Next concrete task

Run one failing native comparison case through the improved observer in the
independent development clone, retaining the actual process outcome. Resolve a
reproducible failure before renewing the full native protected bank. With an
interactive session, qualify normal-window startup. Only then produce and test
the SDK-free installed Windows restart/review workflow. Preserve failed records
and exact source/binary identities. Abrupt death, power-loss durability, scalable
recovery, processing quality, M2–M11, X004/X005 and European language qualification
remain required. The full goal stays active and incomplete.
