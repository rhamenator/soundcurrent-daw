# Installed Windows recovery guards and audition preparation

2026-10-10 UTC. Frozen Reference A/B scope and the full goal remain unchanged.
This extends [checkpoint 158](158-windows-installed-normal-restart.md).
The already installed local preview `20261010183111` remains bound to native
source `4fb8c1cdf1b1365e5834256f4b39b51566dbed52`, tree
`d1b6be0bd9114af21d73a5dd6ba8172b53510c44`. No product input changed or rebuilt.

## Actual installed processes

Separate owned fixture copies preserved the previous proven Applied/Attached
project. Their project, raw media, derivative, completion, binding and export
hashes were captured before/after use. All four installed app processes ran
without compiler/Qt SDK on PATH, used the qualified executable hash and loaded
critical Qt/libsndfile modules from its installed directory.

| Process | Workflow and result | Boundary |
| --- | --- | --- |
| PID 8452 | Ready review; Prepare audition changed its button to Play audition, enabled Stop and disabled Apply; Stop retired preparation and re-enabled Apply; normal exit 0 | Control-side graph preparation only; no endpoint selected and no Play requested |
| PID 9012 | Target start moved from frame 0 to 1; inventory showed Target changed or missing and disabled review; normal exit 0 | Saved fixture state differed only in that start frame |
| PID 2832 | Altered manifest showed Invalid or missing files and disabled review; normal exit 0 | First operator fixture also had an encoding error; not a ratio-only witness |
| PID 2120 | Corrected UTF-8 manifest changed only request ratio 3/2 → 499999/333333; original separate binding remained; inventory showed Invalid or missing files and disabled review; normal exit 0 | Paths and embedded selection snapshot preserved exactly |

The initial Ready project stayed unapplied. Audition preparation displayed
“Audition — project unchanged,” position 0.00 seconds and no missing track frames.
Both output controls stayed at Choose an endpoint and the output peak stayed at
minus infinity dBFS. This qualifies preparation/retirement and the Apply barrier,
not a running WASAPI client, rendered native packets, audible audition or
physical speaker behavior. The guest's emulated audio device uses its existing
SPICE backend; no output route or host audio configuration changed.

Every fixture file compares byte unchanged before/after its app process.
The proven original project's complete file inventory also compares unchanged.
The original raw and derivative hashes agree with checkpoint 158. No new render,
Apply, export, project save or destructive recovery action occurred in these cases.
Normal waits used actual process handles and returned exit 0; no forced kill.

## Encoding failure retained and corrected

The first host audit failed its assertion that only the manifest's ratio changed.
The original manifest was UTF-8 without a BOM. Windows PowerShell's Get-Content
without explicit UTF-8 interpreted its Unicode relative media path and embedded
selection snapshot as ANSI. The original rejection remains valid evidence for
that contaminated manifest, but cannot establish the intended ratio-only guard.

The original capture/fixture remains unchanged. A fresh fixture copied the proved
Ready project and used an explicitly UTF-8 host-generated manifest. Independent
comparison restores the two changed ratio integers and requires complete JSON
equality with the original, including the exact nested selection string. The
new installed process independently refused that corrected fixture. The verifier
also binds its captured opened/final project snapshots byte-for-byte to the
corrected fixture, as for the first three processes. Those links were added after
review identified the omission; the original native records are unchanged. The verifier
audits both the original encoding contamination and the corrected result; it
does not normalize or silently replace either receipt.

Future PowerShell JSON readers must explicitly use UTF-8, particularly for
BOM-free C++-published metadata. This was an operator fixture problem; no C++
project parser or DSP repair was needed. The stale-target fixture came from a
BOM-tagged retained snapshot and independently differs only in start frame 1.

## Evidence and next implementation task

[Original records and verifier](../tests/results/M2/2026-10-10-windows-recovery-guards/README.md)
retain two original capture ZIPs, both executed operator scripts, the encoding
audit, final state and exact member hashes. Bounded recursive guards exclude
products and private transport material. Verification replays neither Windows
nor audio nor archived scripts. This supplements, rather than relabels,
checkpoint 158's earlier normal restart qualification and explicit remaining gaps.

Both build-free clone sessions ended with graceful shutdown. All VMs are off,
both capture receivers have actual exit 0, the owned CD is ejected and temporary
firewall rules removed. Originals, pristine template, equalizers and host routes
remain untouched. Product binaries/installers/dependency archives remain local.

Next: implement an actual-parent interruption acceptance probe with process
identity and terminal observation, starting on Linux without a VM. Cover death
before start acknowledgement, during processing and after completion but before
attachment; reopen through production recovery and preserve incomplete files.
Then qualify the same boundaries on Windows and design bounded, resumable larger
recovery inventories. Existing cancellation/deadline tests do not prove abrupt
parent death. File-flushed receipts do not prove power-loss durability.

Broad M2 processing quality, larger-scale recovery, M2–M11, X004/X005, all-Europe
translation/review/UI qualification, bundled content and native compatibility
remain open. F/Q/C/N gates remain separate; the full goal is active and incomplete.
