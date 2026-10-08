# Windows installer refresh: UI acceptance, capture gate still open

Local build `20261008151000-e91585d523d6` contains the bounded native startup and
ending guards. Application source is `e91585d523d651f75ad4a3fae71ef3d2dae8c8fb`;
the native compiler epoch is `87a74cb196b4ccd746e5d237e73e13d574598129`.
All 344 native build inputs match both commits. Documentation changed afterward.
The installer remains unsigned and paired with exact GPL source and QtBase/
libsndfile source. No binary release has been uploaded.

## What passed

The existing independent `soundcurrent-daw-install-win11` clone installed this
new build slot, verified every payload hash, followed desktop and Start-menu
shortcut targets, and launched/closed the actual main executable normally.
Removal/reinstall preserved an embedded project sentinel and settings. The
actual main again launched/closed normally after reinstall. All five required
Qt/media/platform modules came from the installed directory. Neither compiler
nor Qt SDK was on PATH. Microsoft runtime 14.44.35211 was already installed;
**this is not a fresh OS/runtime bootstrap qualification**.

Setup is 33,759,781 bytes, SHA-256
`82f77b292765f8ca0a1eb9c0ebc02d746390dd90452b83bbea7d2c1e11b0a6f0`.
The actual main is 2,282,496 bytes, SHA-256
`ecba1d7d2643cf0bda8910a6b669ebabe116c5e1ddbc9c5449bba510bbbb051c`.
The native UI fixture is separate test tooling, not an installed product: same
2,330,624-byte executable in all attempts, SHA-256
`852828f76471ae271dd6468c60fab8861ab17f16cb494723010065e7d2c698b3`.
Its loaded Qt/media/platform modules also came from the installed slot.

## Three refused native workflows

The first installed native workflow recorded a complete raw take, then its
playback observer detected an SDK discontinuity. Exactly two bounded unchanged
repeats were attempted; both stopped during initial recording. All actual
fixture exit codes are 1. No successful run replaces these failures.

| Attempt | Fault stage | Preserved raw frames | Faulting capture frames | Expected position | Rejected position |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Playback observer | 480,000 | 462,720 | 463,200 | 463,680 |
| 2 | Initial raw recording | 215,040 | 215,040 | 241,920 | 242,400 |
| 3 | Initial raw recording | 357,120 | 357,120 | 384,480 | 384,960 |

Each rejected packet has the discontinuity flag and a 480-frame gap. The source
bank, original WAVs, journals, first faults, stderr, process/module identities
and failed statuses are retained. Independent verification matches every saved
raw sample to an exact contiguous slice of the original deterministic source;
zero raw error does **not** qualify the incomplete recording/playback workflow.
The cause is unisolated. Host scheduling, QEMU/SPICE and native SDK behavior are
possible investigations, not established explanations.

The developer-clone normal desktop/end-guard results remain valid within their
[separate scope](107-windows-end-guard.md). They do not override these installed
failures. Production-EQ active Stop remains a separate unresolved sample failure.

## Delivery and next task

This refreshed artifact is an **installation/UI preview with experimental native
audio**, not a passed Windows recording preview. Its local INSTALL guide and
QUALIFICATION receipt state that limitation. The previous preview keeps its own
identities/evidence; the existing Ubuntu recording preview is unchanged because
these guard changes affect Windows. Linux physical/sustained qualification remains
open too. No full frozen Bitwig/Cubase parity family is promoted.

[Receipt](../tests/results/X007/2026-10-08-windows-installer-refresh.json) and
[56-payload capsule](../tests/results/X007/2026-10-08-windows-installer-refresh.zip)
retain all three attempts and cleanup. The read-only hosted verifier rehashes,
relocates and checks exact Git inputs and raw samples without native audio replay.
Actual final uninstall exits zero, removes this product and test-only tools,
preserves projects/settings, and observes no native process still running.
Both owned Windows clones are shut down; readonly media and scoped receipt
receiver/firewall rule are removed. VM originals/pristine template are preserved.

Next implementation task: add bounded native packet/lease telemetry with
control-side collection to distinguish SDK-reported capture gaps from adapter or
source starvation; compare initial recording and playback-observer paths on this
clone. Preserve first faults and partial media, keep discontinuity refusal strict,
and qualify the installed workflow before promoting a Windows recording preview.
The production-EQ Stop lease trace remains required independently.
