# Protected transitions from the desktop

2026-10-15. Source `9d040f8e57262ce2eb778899ee34baeb609ab693`.
The full DAW goal remains incomplete. This increment enables the qualified
original protected-span path in desktop transition preparation.

## Behavior

Save and render now selects the protected producer from each clip's original
semantic warp state. It does not infer protection from a slope or retry a failed
protected render with the uniform producer. Ordinary uniform projects preserve
their existing selection format and producer. A protected/uniform pair keeps the
correct producer on each side; an existing-media side launches no render child.
Original protected-history and unsupported fractional/changed-clock geometry
still fail before pair/assembly sidecars are published. Those workflows remain
required future work, not a permanent product exclusion.

The desktop saves trusted references, renders and verifies owned output, and
requires Review/Apply before changing active clips. Apply is one undoable paired
edit. Save remains explicit. No new user-facing strings or production UI source
were introduced, so this increment does not require translation extraction.

## Acceptance workflow

The new desktop bank begins with real owned nominal stretch jobs, verifies their
readiness/completion, adopts them and saves/reopens the original project. It then
drives the actual timeline dialog and controls without inserting a prepared token.
Five cases cover protected/protected at 44.1, 48 and 96 kHz, protected/uniform at
48 kHz and protected/existing media at 48 kHz.

Each successful case verifies fresh job identities, exact semantic protocols,
retired child processes, saved references and unchanged active audio before Apply.
It drives Review/Apply, verifies exact Undo/Redo, saves/reopens and exports a WAV
with floating headroom above unity. Creation, rendered-result, review and applied
screenshots are retained. Original source fixture files remain byte-identical.
The admitted geometry uses exact integer positions; no rounding is added.

Seven additional desktop cases exercise cancellation during preparation, project
replacement, stale clip processing, missing helper, save failure, unsupported
fractional extension and closing during preparation. GUI timers continue during
the gated background work. Failed preparation/save launches no render children.
Save failure retains the atomic in-memory references without publishing a changed
project or activating audio. Unsupported geometry creates no saved pair/recipe/
assembly files. Asynchronous close retires controllers and leaves the saved
project unchanged. The unchanged observation deadline is 30 seconds.

Final qualification: one Debug suite passes in 59.397 seconds. Release has three passing suites in
156.176 seconds: new protected desktop, legacy uniform creation and repeated
rendered edits. The new bank has 12 cases, five adoptions, nine original nominal
workers and nine successfully retired desktop span workers per configuration.
Five export hashes match Debug/Release; nine legacy repeated-edit exports match
the earlier reference. Eight legacy uniform cases preserve their original format.
Source/binary hashes stay stable and selected build targets are up to date.
MinGW syntax validates the changed engine preparation file; it is not native
runtime evidence. The initial Release development probe passed and is retained.

## Preceding native CI

[Original run 38678193915](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38678193915)
at `7a4ffba3bb863e7a6f87266f751b128b587e12a3` passed all six jobs. Its Linux
partition covers 197 unique tests, with disjoint 99/98 assignments. Windows passes
23 comp/core tests, 93 other core tests and 27 desktop tests. This run covers the
protected core/worker/pair recovery changes, not the newer assembly or desktop bank.

All six original artifact ZIPs, terminal APIs, six complete job logs and 94 PNGs
are retained with original sizes, digests and ZIP CRC checks. The previously
missing protected-core folder is present. Native results include 18 core renders,
264 exact map queries, 34,560 protected samples and 12 refusal classes. The worker
bank checks 622,848 complete PCM samples and 12,800 protected samples, with six
successful children and 63 required refusals. Pair recovery has six successful
children plus two cancellation/deadline outcomes. All observed children retire.
Twenty-five original core-manifest files and 18 completed audio files are hashed
against the original reports. This is synthetic native execution, not independent
sound-quality or physical-audio qualification.

Native repeated-desktop evidence includes nine adoptions, three mixed retained
pairs, 15 successful reaped children, five failure/close cases, 502,422 unchanged
outer samples and 508,182 live/export samples. All 14 observations stay below the
unchanged 30-second deadline; the maximum is 5,805 ms. Reviewed native Qt screenshots
have readable controls and the saved applied timeline. Fifteen deployment files
and 1,728 corresponding-source files match the recorded Windows checkout; both
media dependency builds match the reviewed pin. This does not qualify an installer.

[Evidence index](../tests/results/M2/2026-10-15-protected-comp-desktop/index.json)
retains the new bank's complete projects, PCM, requests, process output, screenshots,
source/binary hash records and audits.
[Original CI index](../tests/results/M2/2026-10-15-protected-comp-desktop/original-ci-index.json)
includes the native protected corpus, original job logs, screenshots and audit
receipts; original full ZIPs remain retained locally.

The local acceptance archive contains 8,417 original files in
1,835 unique payloads, 27,875,168 bytes, SHA-256
`0579839607b09550e1b2641c4725f7c937bf4ad2939dcdd92f5c5806c3e7fab7`. The original CI review archive
is 32,123,080 bytes, SHA-256
`ae6056bab792f5c4757a7169a72c12f5246c1bf671a6acd76fad47571e3a9dff`, including 573
original native protected-corpus files. Every archived payload and ZIP CRC was
verified. The earlier successful runs before the ledger correction remain retained
and clearly separated from final qualification.

## Resource assertion correction

Review found that the older repeated-desktop fixture observed an options ledger
which StudioWindow replaced with the actual project ledger. Its zero assertion
therefore did not qualify project memory cleanup. The original CI reports remain
unchanged, and the derived audit explicitly records this gap. The corrected
fixtures obtain the existing window resource-ledger facade, require nonzero
reservations while the project is open and require zero after window, worker and
retained snapshot destruction. Final local qualification includes that assertion
for all new cases and the legacy uniform/repeated cases. New native confirmation
is pending. The earlier successful local runs remain retained as diagnostic
evidence from before this correction. No production UI code changed.

## Remaining work

Obtain the new assembly/desktop bank's native Windows results before claiming that
workflow there. Implement repeated protected-history regeneration and exact
fractional/changed-clock support, with independent processing-quality evidence.
Current installed previews remain unchanged. No audio routes, installed apps, VMs
or equalizer repositories were changed. Full M2–M11/F/Q/C/N/X004/X005 and
all-Europe coverage remain incomplete.
