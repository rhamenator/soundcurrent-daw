# Windows runtime eligibility and observed deployment bundle

Checkpoint 252, 2026-10-13. Full DAW parity remains the goal. This independently
audits the first current hosted deployment bundle and implements a missing runtime
eligibility gate. No new installer is prepared and no VM is started.

## Actual bundle and original run

[Original CI185 run38433376330](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38433376330)
for source `0a5efde4fbb495619d2f04acddad57a23936db4e` passed all four jobs:
Linux161/161 in443.36 seconds, Windows native88/88 in227.67 seconds,
desktop20/20 in73.58 seconds and the core cross-build. The new collector actually
ran on native Windows, with literal source head and tree
`67d454567b70463d442cebc0336bb7e7ca3c7647` bound before and after compilation.

The original deployment artifact11832164320 is104,594,756 bytes,
SHA-256 `3af80f7191b6c136b00a118066ee2e95aecbbcfaecbfc86e800353fb083a48e3`.
Independent Linux ZIP/tar auditing checks the original archive, every recorded
artifact hash, all14 deployment members and all1,493 archived source files against
the pre-build manifest. This is retained-byte auditing, not replay of Windows code.
The original terminal APIs/job logs and original Windows archives remain local.
[Indexed receipts and compressed originals](../tests/results/X007/2026-10-13-windows-runtime-policy/index.json)
contain no executable, SDK, source archive or personal recording payload.

The Qt runtime identities match the reviewed product SDK. Freshly compiled
libsndfile is480,256 bytes, hash
`c2c5d185b51cc55ba18fcdc20da31d5b86664e33db71002645b0b9d31bfe5e1e`;
it differs from the separately reviewed482,816-byte product DLL. The collector
correctly reports exactly `sndfile.dll` as unqualified. Product binary pins stay
unchanged pending independent dependency/native qualification.

## Runtime policy correction

The old NSIS script's runtime check was fixed at14.44.35211.0, even though newer
hosted builds use another toolset. Microsoft requires a runtime at least as recent
as the build tools, with matching architecture; see
[the official supported-runtime guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).
Successful unit tests on a development image with newer installed libraries cannot
establish that an older bundled runtime is suitable for an end user's machine.

`preview.nsi.in` now has a reviewed `RUNTIME_VERSION` substitution. The generator
uses the same trusted policy value in the installed-runtime comparison, source
instructions and receipt. The current trusted version/hash remain14.44.35211.0
and the existing independent runtime pin. Collection or downloading a candidate
does not change them.

The package builder now requires an actual native build collection and its bound
compiler receipt. It refuses a wrong source/head/tree, non-Windows/dirty-input
record, replaced compiler receipt, malformed versions, non-x64 MSVC path or a
runtime older than the required toolset/CRT version. The compiler record must have
an actual CRT header version/hash; old incomplete receipts do not qualify.

## Compiler and runtime patch numbers

The actual hosted compiler directory is14.51.36231, while `cl.exe` reports file
version19.51.36260.0 and product version14.51.36260.0. A compiler executable's patch
number must not be assumed to equal the runtime package's patch number. An initial
policy draft used the compiler product patch as the runtime minimum; inspecting
the actual official runtime download showed that this inference was unsupported.
The final policy uses captured CRT version constants/hash from the toolset headers,
with the toolset directory version as another lower bound. Compiler product/file
versions remain recorded and checked for consistency, rather than silently discarded.

The official x64 permalink downloaded a14.51.36247.0 runtime candidate, SHA-256
`843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c`.
Linux PE resource inspection establishes those version bytes, not a trusted signature
or installed-runtime result. The new native review script downloads the official
package without executing it, checks native Windows Authenticode validity and the
Microsoft signer, retains version/hash/certificate facts and compares the version
against captured CRT requirements. Missing or insufficient CRT evidence remains
explicitly pending. No signing certificate purchase or runtime installation occurs
in this review stage.

## Checks and next gate

The existing Windows deployment-input refusal bank passes with the added runtime
policy cases. A stable toolset directory with a newer CRT requirement refuses the
older runtime; malformed versions and changed source/compiler collection refuse.
The generated actual NSIS script uses the requested reviewed threshold rather than
the old literal. These are generation/policy checks, not installation replay.
The real NSIS compiler also accepts an owned generated syntax fixture with the
14.51.36247.0 threshold; that fixture contains no application and was never installed.
An initial harness import lacked the tools path; it was corrected before generation.
Python and both PowerShell script parsers pass. Native execution of the new CRT
capture/signature review is still required.

Inspect the new original hosted output before updating runtime/dependency pins.
Retain real failures and fix their actual causes, without fabricated identities,
unchanged reruns, extra deadlines or a lower compatibility requirement. Then qualify
fresh main/helper processes and a matching unsigned setup/source pair, using a brief
full independent Windows clone for system-changing tests. Original VMs/template,
Linux preview, equalizers, current runtime/dependency pins and full parity scope are
preserved. Localization remains unreviewed drafts; no UI source/catalog changed here.
