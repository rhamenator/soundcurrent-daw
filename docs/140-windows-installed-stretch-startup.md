# Windows stretch preview installation and startup

2026-10-10 UTC. This checkpoint qualifies installation and startup of the local
unsigned Windows preview from source `f9a63532685c988b4d7203da537957cab41dff92`
and tree `742b786db890a67bbefddcdac8bdb6125d2a481e`. It extends
[the native build checkpoint](139-native-windows-stretch-preview.md).
Installed stretch rendering, editing, export and removal/reinstallation remain open.

## Actual installation

The exact 35,065,933-byte setup has SHA256
`75af398f0c2cbbc36c0f236311745e89f34321ef59e9cde149da7d80dbefe302`.
The paired GPL corresponding source and dependency sources remain local; no
installer, product binary or release was uploaded.

A full independent, flattened clone of the stopped pristine Windows template
used four virtual CPUs and 6 GiB RAM, with its own disk, firmware, UUID and TPM.
No other VM ran. Template disk-chain and firmware metadata remained unchanged.
The test account had no compiler or Qt SDK on PATH.

The first setup process remained live at the harness's 180-second observation
deadline. Later UI reported that the Microsoft runtime could not be installed.
There is no observed terminal exit or consent decision for that attempt. The
subsequent diagnostic found no registered runtime or runtime logs in the two
locations checked; it does not establish the cause of the earlier failure.

The separately observed retry showed Microsoft's signed elevation prompt for
Visual C++ x64 14.44.35211. Consent was explicitly approved. Actual setup PID
4704 exited 0, and the runtime registered as `v14.44.35211.00`. The script checked
all 64 installed payload files against the package's byte lengths and SHA256
values. Both desktop and Start Menu shortcuts target the installed main, and
the per-user uninstall registration points to the correct preview slot.

Two harness errors are preserved: ISO-copied files retained a read-only attribute,
preventing the retry script from overwriting its test metadata; and the shortcut
check initially omitted the build hash from the expected filename. Clearing that
attribute on the owned test files and checking the actual build-named shortcuts
resolved those errors. Neither required a product change or another installer run.
The builder's original `cleanInstallQualified:false` receipt remains unchanged;
this later observation has its own scope and receipt.

## Installed application

Actual main PID 5308 opened in interactive session 1 using an installed-only PATH.
The observed executable hash matches the package. Qt Core, GUI, Widgets, Windows
platform plugin and libsndfile loaded from the installed preview folder. The real
GUI opened the owned Unicode project `project-été-Κиїв` and displayed its track
and clip. Project and raw-media bytes remained unchanged. Normal window close
exited 0. No playback, recording or stretch render was started.

The VM was shut down after the short session. The bounded capture receiver and
temporary scoped firewall rule were removed. Original VMs and equalizer working
trees were preserved.

## Retained evidence and next task

[The qualification receipt](../tests/results/M2/2026-10-10-windows-installed-stretch-startup/qualification.json)
binds a 30-member capsule of raw process/setup reports, commands, owned project
and audio, consent/project screenshots, harness failures and cleanup observations.
It contains no credentials, private transport key, installer or DLL payload.
These are maintainer observations; hashes do not authenticate an untrusted receipt
or replay the installed files. The payload checks ran in the guest before the
report was emitted; individual installed binaries are not retained publicly.

Run the portable retained-data checks with:

```sh
python3 tests/results/M2/2026-10-10-windows-installed-stretch-startup/verify.py
python3 tests/results/M2/2026-10-10-windows-installed-stretch-startup/refusals.py
```

The eight cases reject inflated outer/nested scope, developer SDK module paths,
invented first-attempt exits, altered builder scope, project state and raw samples,
including changes with consistently recomputed archive manifests.

Next reuse this stopped independent clone to qualify the installed default
stretch helper: render duration 3/2 and independent pitch +7.00007 semitones with
preserved formants, review/apply, Undo/Redo/save, new-process reopen and repeated
WAV exports with independent PCM/headroom checks. Then verify normal removal and
reinstallation preserve the project, media and settings. Physical audio, complete
processing quality, all-Europe language delivery and the full frozen functional,
quality, content and compatibility requirements remain open.
