# Verified native original WAV export

2026-10-12. The full suite goal remains active and incomplete.

The native recording panel now exposes **Export original WAV…** after a take has
stopped. Choose a new destination; existing files are refused. The export receipt
shows the path and native frame count. Export runs on the existing control worker;
Cancel and asynchronous close can interrupt work before publication. Keep/reset
releases the displayed receipt without deleting the captured take or exported file.

This exports the original floating-point RF64 WAV byte for byte. It preserves
signed zero, headroom, sample rate and channel count. Monitoring EQ, channel mapping
and project timing are not printed. A failed or cancelled monitoring run can still
have a verified finalized capture; its original status stays in the receipt. A
pending, empty, nonfinalized or nonfinite capture cannot be exported by this action.

The worker pins the source before verification, verifies the expected native
context and sample/stamp binding, and validates that the entire RF64 frame extent
matches the committed original prefix. Recovery's common-prefix verification alone
does not prove that additional file audio is verified. A test adds an RF64 tail,
updates its container size/sample count, confirms the original common prefix still
verifies, then requires export refusal. Existing approved WAVE validation is reused;
no new external dependency is introduced.

A reserved 64 KiB streaming buffer copies the original container. Source and
completed temporary-file hashes must match before publication. Publication cannot
overwrite a racing destination. Cancellation removes the owned partial file and
preserves the original. After publication, directory-flush/alias-cleanup failures
are returned as warnings with a completed-file receipt. Windows reports file flush
durability; a directory flush is independently available on Linux. These are
cooperative I/O cancellation and admitted workspace bounds, not OS memory/deadline
limits or permanent snapshot/result ownership qualification. The desktop's current
source-size policy is 1 GiB; the core exposes a trusted maximum up to 64 GiB.

No project-time anchor is inferred from monitor ordinals, capture positions or GUI
duration. The native original is still not automatically attached to the timeline.
Reopening a retained controller result works; browsing/reopening native takes after
an application restart and verified timeline attachment remain open.

## Qualification and retained failures

Implementation/source `93348d4c19fc0905a591bd012d4b327fc068b42e` has 786 exact
source/checkout inputs bound in the [qualification packet](../tests/results/M2/2026-10-12-native-original-export/summary.json).
Release core/localization/panel passes 0.57/0.63/9.87 seconds (11.07 total),
including 1,608 durable native outcome checks and 73 recording-panel checks.
ASan/UBSan passes 18.41/1.62/11.19 seconds (31.22 total), with 14 fresh changed
or dependent objects and four private archives; unchanged qualified dependency
archives remain read-only and hash verified. The actual Linux application entry
point links. GUI TSan remains unqualified.

Five current Windows core/controller/SDK/fixture objects cross-compile with strict
warnings and no warnings. This does not establish native Windows link/runtime,
physical-device or installed-current-preview qualification. Linguist update,
structural audit and 13 corruption/refusal workflows pass: 34 catalogs, 966 keys,
3,168 draft translations, zero promoted native reviews and zero fully qualified
languages. Original logs, commands, source/object/archive/executable hashes and
failure source are retained; the public packet contains no executable or audio.

The initial success fixture supplied only 1 MiB where native inspection requires
approximately 8 MiB before its smaller file workspace. It was corrected to the
existing normal 16 MiB policy; the explicit one-byte quota refusal remains. The
first desktop extent run successfully exported and refused overwrite, but its test
clicked Keep while the last UI projection still had that button disabled. The test
now polls the observed post-refusal snapshot before clicking, following the panel's
existing acceptance pattern. Both original failures, diagnostic run and successful
intermediate core run are retained. No production deadlines were relaxed.

## Preceding Windows device selection

PR155/run38205159475 passes all four jobs: native Windows71/290.15 s,
desktop Windows20/57.34 s, Linux144/322.69 s and the core cross-build. The actual
Windows desktop fixture executes 66 selector/panel checks with simulated SDK
devices. Original logs, artifact JSON and exact tested source bindings are retained
in the [runtime packet](../tests/results/M2/2026-10-12-native-device-selection-runtime/summary.json).
That candidate predates this export; it does not qualify export on Windows,
physical devices or an installed current preview. Historical failure causes are
not declared resolved by later green runs.

## Next implementation task

Qualify this export in current Windows CI, then add explicit project-owned native
original take selection, reopen/inspection and export across application restarts.
Keep monitoring outcome, original capture and proposed timeline alignment separate.
Native Linux adapter integration, custom mapping, automatic idle device refresh,
permanent ownership, current installed/physical qualification and all later scope,
processing quality, bundled content, compatibility, imports, profiles and European
language qualification remain required.
