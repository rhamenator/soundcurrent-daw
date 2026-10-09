# Native Windows stretch preview preparation

2026-10-09. Source `f9a63532685c988b4d7203da537957cab41dff92`, tree
`742b786db890a67bbefddcdac8bdb6125d2a481e`, qualified through protected PR80
and merged as `8f65dfc6dd3aa078f810ee033cbd7d3f652a6a1e` with identical trees.
This checkpoint adds actual native build/process evidence and a local unsigned
installer/source pair. Clean-installed Windows stretch acceptance remains open.
The frozen functional, quality, content and compatibility scope is unchanged.

## Actual native build and processes

The independent Windows development VM used a real Git sparse checkout of that
commit, excluding historical result media. All 661 selected source inputs match
canonical Git bytes. Initial line-ending restoration left stale index metadata;
refreshing the index with filesystem caching disabled produced the exact HEAD
tree and clean tracked status before configuration and after the build. No source
change or fabricated commit identity was used to pass the gate.

Visual Studio 2022/MSVC 19.44.35228 built the Release main, four helper executables,
artifact verifier and two desktop test executables with parallelism two. The
reviewed deployment SDK remains Qt 6.12.0 and libsndfile 1.2.2. Hosted CI's different
SDK is not an installer dependency qualification.

Actual inspection, media checking and checked-copy/recovery children retain
observed PID, executable, source and protocol identities. A qualification script
initially requested a nonexistent `sha256` field in the versioned copy provenance.
The corrected script invoked a new actual recovery child and checked the defined
`sourceSha256`/`stagedSha256` fields against independent owned WAVE bytes. The
original failure is retained; the correction does not reuse a reconstructed PID.

The actual native stretch producer passes 173 workflow checks, 34 completed jobs,
the valid-span 32 MiB resource refusal and 256 MiB positive control. The separate
artifact executable reports 1045 shared live/export/save/reopen checks. Its
process receipt has 175 checks including two source/executable identity checks.
Native Qt UI and controller tests each report 48 checks. These are synthetic
workflows without opening an audio endpoint. The actual main opened in interactive
session 1 with the SoundCurrent DAW title and exited 0 after normal window close;
it used the developer SDK PATH.

## Local installer and GPL source pair

Sequence `20261009232400` uses five application executables, including the four
helpers, and the exact nine reviewed runtime DLLs. Native deployment ZIP membership,
CRC, sizes and hashes match the manifest. The normal preview builder checks native
main/helper receipts and adds licenses, notices, SBOM and source instructions.
Its 64 payload files and all local artifact hashes were independently checked.
The complete application source archive matches 662 selected source/license/
packaging members, including all 661 native build inputs and vendored stretch code.

| Local artifact | Bytes | SHA256 |
| --- | ---: | --- |
| `SoundCurrent-DAW-20261009232400-f9a63532685c-x64-setup.exe` | 35,065,933 | `75af398f0c2cbbc36c0f236311745e89f34321ef59e9cde149da7d80dbefe302` |
| `soundcurrent-daw-20261009232400-f9a63532685c-source.tar.gz` | 720,996,128 | `076a89a9e5cd1a0da02d8e721abadbbfc661a68afb3c33cd3985edbfeba8e776` |

The local folder is `.cache/windows-stretch-preview-20261009232400/` in the DAW
workspace. Complete pinned QtBase/libsndfile source archives are alongside setup,
under `dependency-source/`. Preserve the whole pair for GPL source delivery.
The SoundCurrent wrapper is unsigned and contains the unchanged separately signed
Microsoft runtime installer. No driver, VB-CABLE, compiler or Qt SDK is required
by the product payload; that dependency property still needs clean-install testing
for this revision. No installer binary was uploaded or installed in the development
VM. All VMs are stopped; original Copperfin VMs/template and equalizers were preserved.

## Retained evidence and next task

[The capture receipt](../tests/results/M2/2026-10-09-windows-stretch-preview/qualification.json)
binds a small [capsule](../tests/results/M2/2026-10-09-windows-stretch-preview/capture.zip)
with raw build/native logs, source input hashes, observed process receipts, owned
inspection/WAVE data, exact deployment and package metadata, commands and failures.
It contains no product binaries, SDK, credentials or personal recording. Temporary
stretch-render samples were checked by the actual producer and discarded; this
capsule cannot independently replay or recompute their audio hashes. Maintainer
observations and hashes are not authentication of an untrusted receipt.

Run `python3 tests/results/M2/2026-10-09-windows-stretch-preview/verify.py` for
retained-data checks. `--check-current-inputs` additionally verifies whether the
present working files still match the historical 661 inputs. This is not Windows
execution or installer replay.

Next use a full independent acceptance clone of the pristine Windows template.
Install this exact pair, verify shortcuts and loaded modules with no developer SDK,
then exercise the actual installed default helper on a Unicode fractional-anchor
project: render/review/apply, Undo/Redo, save/new-process reopen, repeated WAV exports,
and normal remove/reinstall with user-data preservation. Check raw/derived/export
samples, duration, headroom and executable identities independently. Start only one
VM and stop it promptly. Physical recording reliability, short-span context,
dynamic warp/segmented pitch, full processing quality and European localization
remain separate required gates.
