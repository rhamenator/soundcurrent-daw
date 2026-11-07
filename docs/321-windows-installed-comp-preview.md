# Windows installed preview and native CI partition

Checkpoint 321 qualifies installation of the unchanged source `2580164`, sequence
`20261015124133`, in the existing independent Windows acceptance clone. This is a
local unsigned preview, not a published release or full DAW parity claim.

## Actual installer and installed workflow

The installer SHA-256 remains
`79d77416777ab9d6cd090120d8b73d4cd45d601e00e506db7b66e025d2774cd0`.
The clone has no development SDK used by this test; neither `cl.exe` nor `qmake.exe`
was found on its PATH. It is an existing acceptance installation, not a pristine OS.

The installer upgraded the shared Microsoft runtime from `14.44.35211` to
`14.51.36247`. The native elevation prompt identified Microsoft Corporation as
the verified publisher; it was accepted on the test clone. Setup returned zero.
All 65 installed payload files matched the package receipt. The versioned desktop
and Start menu shortcuts and uninstall registration pointed to the new per-user
installation.

The actual installed application opened a copied Unicode-path project, saved and
closed normally. A second launch reopened it, exported through the real Export
audio dialog, saved and closed normally. PIDs 7740 and 8680 each returned zero.
Qt, its Windows platform plugin and libsndfile loaded from the installed directory.

The 1,920,080-byte WAV is byte-identical to the
[independently checked Linux export](320-installed-linux-comp-preview.md):
480,000 mono float samples at 48 kHz, including preserved floating-point headroom.
SHA-256: `1aa32b4d823b36c1272c72054da30e002a07c5ac72560685e3db8daefdf9d59d`.
Saved project JSON and raw media hashes remained unchanged.

## Removal and reinstallation

The actual uninstaller and reinstaller both returned zero. Removal deleted the
owned payload, shortcuts and uninstall registration. It preserved the copied
project, a test preference, and a user-created file inside the installation folder.
Reinstallation restored the exact 65 payload hashes and correct shortcuts and
registration while preserving those same user files and settings. The preceding
preview's file inventory remained unchanged throughout. The new preview is left
installed for later editing tests.

No physical audio endpoint was activated. All application processes ended before
the clone shut down gracefully. Original VMs and the pristine template were not
started. The temporary capture listener, narrowly scoped firewall rule and test
CD attachment were removed after the test.

## Original CI outcome and correction

[Run 38742290790](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38742290790)
at documentation head `6fd99c5` ended **cancelled**, with eleven successful jobs.
The native Windows check's annotation states that the job exceeded 30 minutes.
Its log records all 43 comp tests passing in 540.72 seconds and all 93
recording/media tests passing in 240.94 seconds. The combined build, both test
banks and evidence upload exhausted the job's total budget. These passing test
results do not override the cancelled required check.

The original 12 job logs, APIs and all 11 artifact archives are retained locally;
all archive sizes, digests and CRCs were verified. Total archive size is
1,989,725,910 bytes. Product code is unchanged from the earlier fully passing
candidate run; the intervening changes are documentation and retained evidence.

The workflow now runs the two existing native test banks in separate jobs. The
seven configure/build/test command lines are unchanged; both groups retain the
30-minute limit and all individual test deadlines. The recording group keeps the
original failure-retention tests and three concurrent-owner diagnostics. Evidence
uses separate artifact names. Each group currently builds the original target
inventory, avoiding an unverified change to helper availability.

The existing required check name, **Windows native packet ownership tests**, now
aggregates both groups and rejects failure, cancellation or skipping. Matrix
fail-fast is disabled so one group's failure cannot cancel the other. YAML parsing,
command equality and aggregate rejection behavior were checked locally. Native
execution of this changed workflow remains pending; no unchanged CI rerun or
deadline increase was used to hide the original outcome.

## Evidence and next work

[`acceptance.zip`](../tests/results/M2/2026-10-15-windows-installed-comp-preview/acceptance.zip)
contains original guest captures, scripts, installation/workflow/cycle receipts,
the retained CI logs and metadata, and verification summaries. Its adjacent
manifest binds the archive. Large original CI artifacts remain in the maintainer's
local `.cache/windows-installed-comp-321/ci/` directory.

Next: observe the partitioned native CI and exercise short comp rendering,
Review/Apply, Undo/Redo and save/reopen/export in the installed preview. This
checkpoint does not establish that editing workflow, clean-OS installation,
physical hardware behavior, complete localization, processing quality, content
coverage or full professional DAW parity.
