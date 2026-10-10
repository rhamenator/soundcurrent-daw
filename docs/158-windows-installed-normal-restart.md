# Installed Windows normal render restart

2026-10-10 UTC. The frozen Reference A/B scope and full goal remain unchanged.
This extends [checkpoint 157](157-native-windows-restart-preview.md), using the
already installed local unsigned preview `20261010183111`. Its native source is
`4fb8c1cdf1b1365e5834256f4b39b51566dbed52`, tree
`d1b6be0bd9114af21d73a5dd6ba8172b53510c44`. The documentation/evidence merge
`7030049` did not change its C++ inputs; this checkpoint does not relabel the
installed binaries as a new build.

## Actual installed workflow

The SDK-free independent acceptance clone used the same Unicode-named project
through all three normal installed app processes. Actual process handles, paths,
start times and hashes are retained. None of these steps used a Qt test fixture.

| Step | Observed outcome | Limit |
| --- | --- | --- |
| Render and normal quit | Main PID 3376 rendered protected operation `eee85921-5fd6-497f-88be-ecfd1a260bca`, then exited normally with code 0 | Project and raw media stayed byte unchanged; result was unapplied |
| Actual helper observation | PID 6584 matched the installed helper path and qualified SHA-256 | Polling captured the helper; module/parent/terminal-helper observations remain incomplete |
| New-process Ready review | PID 6568 reopened the same project; explicit inventory/review independently verified Ready and restored duration 3/2, pitch 0 and marker 4096 → 6144 | No automatic attachment; supported UI Automation actions used |
| Marker guard | Changing target to 6145 disabled Apply and audition; restoring 6144 enabled both | No new render required |
| Apply and saved history | Apply saved the 49,152-frame derivative; saved Undo restored the raw clip; saved Redo exactly restored the applied state | Undo comparison accounts explicitly for schema minor 12 → 15 normalization |
| First export and normal quit | Canonical export completed inside the project's `exports` directory; PID 6568 exited normally with code 0 | Float32 stereo, 48 kHz, owned synthetic source |
| Second new process | PID 3644 reopened the saved project; inventory showed Already attached and disabled review | Duplicate adoption refused |
| Repeated export and normal quit | Second export was byte identical to the first; PID 3644 exited normally with code 0 | No audible/native playback or audition exercised |

The main executable SHA-256 is
`2302a5d5d2da63476a2f1f7074053b64d75a379d31973916bc35881ae29d57bd`;
the helper SHA-256 is
`ca352ca8325219b0dce6a61029878a76f85fc2cdd8d42d75f102b4ba248a2731`.
Critical Qt/libsndfile modules loaded from the owned installed directory; no SDK
was on the app PATH. The existing runtime and older preview installations remain;
this is not pristine installer/runtime bootstrap qualification.

## Independent audio and persistence checks

The retained verifier checks raw/derivative/manifest bytes, source binding, complete
receipt hashes, every float sample and saved project states. The original raw
WAV remains unchanged. The derivative contains 49,152 finite stereo frames at
48 kHz, retaining peak 1.5 linear rather than clipping internal headroom.

The protected core is bit exact. The full protected span is numerically equal
to the original; its blending halo has **128 signed-zero bit differences**. Both
exports are byte identical to each other and numerically equal to the complete
derivative. There are **2,175 signed-zero bit differences** between exported PCM
and derivative PCM. These counts are independently computed from this current
capture; they are not copied from a historical preview's results. Neither
comparison claims complete derivative/export bit equality or broader processing
quality.

Saved Undo differs from the initial JSON only in schema minor 12 → 15. Its raw
asset, clip, processors, routes and other project state compare semantically equal.
Saved Redo and the second process's project file equal the applied project.
The immutable parent selection, intent, completion and derivative bytes remain
unchanged across review and attachment. File-flushed receipts do not prove
power-loss durability.

## Retained operator failures

Original failures remain in the evidence with their actual live process identities:

1. The first recovery observation counted all ListItems, including the main track
   list, and hit its inventory deadline despite a visible Ready entry. Filtering
   by the actual recovery-list identity corrected the observer.
2. The next assertion expected table-cell Value fields to contain numbers. The
   provider exposed empty Value fields and correct accessible names. Checking
   names verified the exact recovered controls; ValuePattern writes successfully
   exercised the marker guard.
3. The first export targeted the project root and was refused with the existing
   `exports` directory restriction. Its deadline remains failed. Menu invocation
   also left the menu open, so Ctrl+S did not save the intermediate Undo snapshot;
   identical intermediate hashes are retained without an Undo-success claim.
   Explicit Save action, menu dismissal and observation of the saved model then
   qualified saved Undo/Redo and the allowed export destination.
4. Dismissing the menu after the review action closed the newly opened modeless
   inventory dialog, causing another observer deadline. The corrected continuation
   retained the same actual PID 3644 and qualified Attached refusal/repeated export.

No app was killed/restarted on an observation deadline. Each continuation guarded
the same actual owned process, installed path and payload hashes. No DSP source
change or extra derivative render was used to resolve these operator failures.
The previous main launcher's 600-second and helper observer's 900-second limits
are also retained; neither establishes process termination.
The first host-only RIFF reader refused the legitimate RF64 derivative; the
existing RF64-aware parser corrected that diagnostic. A full-span bit-equality
assertion also failed on signed zeros. The numerical/core comparisons preserve
that boundary instead of treating the failed bit-equality assertion as success.

## Evidence, cleanup and next task

[Retained records and verifier](../tests/results/M2/2026-10-10-windows-installed-normal-restart/README.md)
preserve original capture ZIPs, operator scripts, byte manifests and bounded
claims. Verification runs without replaying Windows, audio or archived scripts.
Recursive evidence guards exclude product binaries and private transport material.
The preview installer/source/dependency archives remain local; no release upload.

One build-free independent clone session performed the fresh-process acceptance.
All VMs are off, the acceptance CD is ejected, the receiver's actual exit is 0 and
the narrow temporary firewall rule is removed. Originals, pristine template,
equalizer repositories and host audio routes remain untouched.

Next: qualify installed Windows changed-target/altered-selection refusals and
Prepare/Stop audition on an owned virtual endpoint, preserving original fixtures.
Then exercise abrupt parent death/interrupted output recovery and bound larger
recovery inventories. This normal Ready/Apply/history/Attached/export acceptance
does not complete those gates. Broad M2 quality, M2–M11, X004/X005, all-Europe
language qualification, bundled content and native compatibility remain open.
The full goal stays active and incomplete.
