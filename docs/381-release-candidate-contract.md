# Release candidate contract

Owner policy,2026-10-27. This governs official candidates; it does not promote a
scoped development preview or reduce the frozen product baseline.

## Scope and version

Official candidates remain below1.0 and meet the same technical, security,
compatibility, documentation, artifact, adversarial and owner self-review gates
as production releases. Functional parity, processing quality, bundled content,
native-project compatibility and localization retain separate acceptance evidence.
An absent independent human review must be disclosed; it is not a hard blocker.

Current supported desktop operating systems are **Windows and Linux** only.
Architecture may retain low-cost portability. macOS implementation, packaging,
testing, signing/notarization, release claims and gates receive no current release
effort. Demand may justify reconsideration later; no future free/paid model is promised.

## Artifact and native qualification matrix

Each declared supported OS/version/architecture needs its own exact-source artifact
and evidence row. A cross-build or portable archive does not satisfy native install
qualification. The examples below are formats, not new support/version commitments.

| Target | Required delivery | Required disposable-system evidence | Current qualification |
| --- | --- | --- | --- |
| Declared Windows targets | Native installer, such as the existing EXE setup | Clean install, normal-user launch, upgrade from declared predecessor, uninstall, residue inventory | Scoped historical previews exist; official RC gate open |
| Declared Linux targets | Recognized native package, such as the existing DEB | Clean install/dependency resolution, normal-user launch, upgrade from declared predecessor, uninstall, residue inventory | Scoped historical previews exist; official RC gate open |

Tests use disposable supported systems or independent clones, preserving originals,
pristine templates and user data. Record artifact/source hashes, OS/architecture,
prerequisites, exact actions, exit receipts and expected versus observed residue.
Uninstall must preserve projects/recordings; document retained settings/cache and any
explicit purge choice. A successful archive extraction is not an installer test.

## Unsigned candidates and integrity

Commercial signing is deferred until funding exists. Installer implementation and
qualification are not deferred. Clearly label unsigned candidates and document actual
trust prompts or policy refusals without disabling platform protections. Checksums
establish integrity, not publisher trust. Publish checksums for every installer,
package and corresponding-source artifact, with source revision/build inputs and
provenance attestations where available. Disclose unavailable attestations and human
review instead of inventing evidence.

## Publication gate and workflow boundary

Before official publication, retain one candidate-specific evidence index tying the
exact source/artifacts to all required technical, security, compatibility,
documentation, adversarial and owner self-review results; close or explicitly resolve
remaining release blockers under the owner requirements. Include installer lifecycle,
GPL corresponding source/notices, dependency/content rights and per-language status.
Translations without native review/UI qualification never count as supported languages.

The current Development checks workflow supplies source/build/test evidence. Its
Windows review packages and uploaded archives are development artifacts, not official
RC delivery or install qualification. There is currently no official publication job;
a future one must enforce this contract and the existing publication authorization.
Do not add macOS to its release matrix. No candidate is approved by this document.

The frozen Windows preview producer and original failed/unfinished native evidence
remain pinned. Later source milestones do not retroactively qualify that installer or
move its artifact inputs. See the installation contract and its scoped preview records.
