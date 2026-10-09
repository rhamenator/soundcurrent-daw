# Explicit desktop media checks

Date: 2026-10-09. Frozen baseline SC-DAW-BASELINE-2026-10-05 unchanged.

## Preview workflow

Open a supported project inspection, then **Check media…**. The virtual table
lists the original WAVE SourceFile ID20 evidence, including missing and duplicate
references. Select a row and choose its media folder, or explicitly select a
replacement file. Check results show source format and sample peak. Cancel stops
the child; closing waits asynchronously for its terminal retirement. Clear local
choices releases the results and selected folder. No audio device is opened.

Choices are temporary and are excluded from saved `.scinspect` bundles. Opening
or reopening an inspection grants no media access. Original reference bytes and
the current project stay unchanged. Duplicate references and ambiguous takes
cannot silently select an occurrence; unsupported source types remain in the
original inspection evidence rather than becoming WAVE assets. A missing FILE
property can receive an explicitly chosen replacement for this check.

This is an inspection workflow. It does not copy media, convert a foreign project,
add tracks, render its semantics, or create an Undo command. Complete imports
remain required, including all registered native and exchange families.

## Ownership and process boundary

Original GPL-3.0-only `sc-wave-report` has no Qt/audio/decoder dependency. It
independently validates the exact flat v2 JSON schema, admitted same-ledger banks,
actual observed child PID, exact selected reference, complete frame counts,
format/precision/layout, finite peak/headroom, extent/work limits and hash syntax.
It validates the report boundary; it is not a second independent audio decode or
independent source checksum.

`WaveCheckController` admits one immutable selection and one child at a time on
a low-priority control/I/O thread. QProcess is worker-owned; no shell is used.
Parent stdout/stderr banks are 8192/1024 bytes. Cancellation and the default
20-second deadline terminate the child, then escalate to kill after 200 ms.
Output overflow kills it. The exact child owner and work credit remain held until
the process is terminal. Only known stable diagnostics reach translated UI text.
Exact LF and Windows CRLF diagnostic endings are accepted and tested.
Malformed, duplicate, nested, wrong-PID or inconsistent reports publish no result.

The GUI selects a path; the child approves and opens the directory at execution.
That GUI string is not an already-open parent capability. Existing approved-root
and pinned-file rules then apply. The separate native-leaf API is used only for
explicit replacement selection; it permits a Linux nonportable filename without
normalizing a foreign reference, following links, traversing directories or
opening a Windows alternate stream. Linux unchanged-object checks are not a
hostile-filesystem snapshot.

The default original-file limit is 1 GiB. The GUI exposes 1–8192 MiB per check;
existing RIFF/provider/read/frame/channel limits can still refuse a larger file.
The child has 16 MiB of declared payload admission, plus parent banks/parser/value
grants and Qt work allowance. These are not hard OS RSS/CPU or sandbox limits.
Qt buffers, decoder internals, filesystem behavior and uninterruptible kernel I/O
need separate containment/platform qualification. No hard termination deadline
or full security isolation is claimed.

## Qualification

Twelve selected Linux Release tests and eight ASan/UBSan tests pass (leak
detection disabled), including 85 actual controller checks and 45 media UI checks.
The screenshot run adds one successful image-save check. MinGW compiles the core
report/root targets only; it makes no Qt, WAV or native-runtime claim.

Local source/binary hashes, exact logs, screenshot, repairs and prerequisite
native evidence are retained in
`tests/results/X004/2026-10-09-desktop-media-checklist/`. The receipt distinguishes
current checks from the already-merged PR67 WAV validation cohort.

| Acceptance workflow | Current evidence | Remaining qualification |
|---|---|---|
| Actual Unicode child, PID/reference binding, normalized samples, owned report/borrowers | Linux controller checks | New native Windows Qt run |
| Missing media, corrupt reports, single flight, early/live cancel, deadline, output flood, credit retirement | Actual children and controlled report corruption | OS containment and storage/filesystem breadth |
| Explicit folder/replacement, original tokens, missing/duplicate/unsupported evidence, safe labels | Actual Linux Qt UI and owned source media | Native/interactive/installed workflows |
| No automatic reopen access; source-only save; clear choices; asynchronous live-child close | Actual GUI/controller acceptance | Crash/power-loss and installed workflows |
| Format/sample peak visible, English channel plurals, source inventory | Screenshot and catalog/UI checks | Reviewed translations and all-Europe UI qualification |
| Both workers required with exact qualified binary/source inputs | Linux DEB modes/dependencies and Windows deployment-input refusal tests | Fresh installer pairs and actual installed execution |

The new strings are inventoried in all 34 draft catalogs (714 source keys).
The existing 3135 non-English draft translations are retained; added strings fall
back to English where untranslated. No language is promoted to reviewed or fully
UI-qualified. Localization remains required across Europe on both platforms.

Native Qt CI builds the real app and new controller/UI tests under the existing
required context. All four strict required contexts and admin/PR/linear-history
protection remain intact. CI uploads logs/runtime metadata only. This increment
does not refresh the previously installed preview or upload product releases.

## Next implementation task

Design and implement verified, transactional copying of approved media into a new
owned destination with original-reference/object mappings. Recheck content and
identity before committing a copy, preserve losses, and leave both source and
current project intact on refusal/cancel/storage failure. Then extend destination
timing/gain/fade/rate/pitch state, preview conversion, and qualify Undo/reopen and
independently aligned renders. RF64/W64/compressed/partial-precision WAVE and other
formats stay required. Full functional/quality/content/native parity is incomplete.
