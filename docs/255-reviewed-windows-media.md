# Independently reviewed Windows media DLL

Checkpoint 255, 2026-10-13. The libsndfile binary pin is updated after source,
build-configuration, byte-comparison and actual native media/worker review.
App-window and installer acceptance remain separate, pending gates.

## Build and source review

[Original run 38448279083](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38448279083)
for source `7b67b539a149789e888896fe2fc1b248b672c69c` passed all four jobs:
Linux 161 tests (397.06 s), native Windows 88 tests (216.71 s), Windows desktop
20 tests (83.10 s), and the Windows cross-build. The native desktop checkout is
the exact source head; other jobs retain their original checkout identities.

The [indexed review packet](../tests/results/X007/2026-10-13-reviewed-windows-media/index.json)
contains original logs, build caches/rules, observed native results and the
executed media/helper harnesses. Complete original binary/source archives remain
local; this packet publishes no DLL, SDK, installer or audio fixture.

Both fresh native x64 Release builds produced the same 480,256-byte DLL:

`2f50a0b47e46407bf9d3d34f95dd58289356020331ebd989a0c0a924a57fb6e7`

Their link flags were `/Brepro /PDBALTPATH:sndfile.pdb`. Independent inspection
checks the source archive hash, both actual DLL hashes, native configuration,
14 deployment files and all 1,500 build-source files. This demonstrates
same-environment reproducibility. Cross-machine/toolchain reproducibility has
not been established.

The source is unmodified libsndfile 1.2.2, SHA-256
`ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121`,
with external/MPEG codecs disabled. Its actual source archive includes the LGPL
2.1 COPYING text; original notices remain intact. Shared-library replacement and
matching corresponding-source delivery remain required by preview packaging.

## Actual native observations

The existing independent development clone ran alone with 6 GiB RAM and four
vCPUs. It already had Microsoft runtime 14.51.36247.0; no runtime installation
was performed. A fresh sparse Git checkout retained the exact source head/tree
and all build inputs while excluding historical test-result media.

- The review process loaded the actual deployed DLL and verified its module path
  and complete hash, then called its native API.
- Original 8,192-frame stereo float WAV and RF64 fixtures round-tripped with every
  PCM byte preserved. Their peak was 1.79999995, above full scale.
- Seeking to frame 257 and reading 37 frames was exact. Accented, Greek and
  Cyrillic paths worked. An invalid audio header was refused.
- Four actual inspection/probe/import/recovery processes returned their observed
  PIDs, exit 0, expected protocols and byte-identical source/copied media.
- The waiting copy worker's loaded module path/hash matched the deployed DLL.
  Raw fixture and copied asset bytes stayed identical.

No physical endpoint was activated. No real-time, listening, stretch-quality,
installed-app or broad codec parity claim follows from these lossless file checks.

## Pin decision and remaining delivery work

`research/windows-preview-dependencies.json` preserves the previous SDK identity
and the review provenance, then adopts the exact reviewed DLL. Qt identities stay
unchanged. The package input/refusal tests pass; the actual candidate manifest is
accepted and the prior SDK identity is refused. A self-consistent replacement
manifest cannot update this policy.

A setup-script quoting error and a rejected stored console password are retained
locally. SSH file-based tests succeeded, but there was no logged-in desktop
session, so no current app-window qualification is claimed. The owner was asked
for console sign-in; the clone was shut down after its checks to limit resource
use. Originals/template, qualified caches, equalizer repositories and the installed
Ubuntu preview remain unchanged.

Next build this reviewed-pin source, verify the fresh DLL still matches, then
obtain actual interactive main/helper/worker receipts and prepare its matching
unsigned installer/GPL source pair. Installer acceptance uses an independent
clone. No release binary was uploaded or protected branch merged. Full DAW parity
and European localization remain active and incomplete.
