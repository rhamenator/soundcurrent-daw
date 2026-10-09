# Linked clip playback-rate controls

Date: 2026-10-09. Frozen baseline remains SC-DAW-BASELINE-2026-10-05:
Bitwig6.1.3 plus Cubase Pro15.0.30. Full F/Q/C/N and all-Europe coverage remain
incomplete.

## Previous source qualification

PR75 source5b8e35ce38ef4b3cd653474a467160fd20428f79 merged as
902fc7723593ca31c021c257aa9ed42a3b1b1b2c with identical trees.
Run37952810083 passed99 Linux,34 native Windows core and9 Qt tests plus
cross compilation and unchanged protected checks. Final receipt and digest/CRC
verified native logs are retained with this checkpoint. They qualify the preceding
positioned reader, separately from this increment. Native audio and installed
positioned playback were not inferred.

## Usable increment

Select a clip, set Playback speed (pitch follows), and Apply speed. The control
supports0.250..4.000 times normal speed, exact stored ratio display, accessible
naming, locale formatting and an unfocused-wheel guard. Stop prepared audio before
editing. Undo/Redo restores the exact prior clip. Save/reopen retains versioned
state. Both Linux and Windows use the same engine and Qt controls; each platform
requires its own qualification.

Changing speed keeps source origin and timeline start, retimes duration and fade
anchors, and respects complete asset extent. Raw audio remains immutable. Export
range stays as set; extend it when needed. Split/crop/seek and integer source edits
honor effective speed. See [ADR098](decisions/098-linked-clip-playback-rate.md) for
exact arithmetic, bounds, rounding/migration and worker/resource ownership.

The existing pinned BSD kernel now prepares the effective source/project ratio.
No new dependency or proprietary algorithm is adopted. Existing unit-speed
samples must retain their qualified waveform. Processing is shared by live
read-ahead and offline WAV export. The native callback still consumes prepared
slabs, with no file I/O, GUI, allocation, blocking locks or kernel work.

## Acceptance and gaps

Qualification is pending until the retained receipt records exact input hashes,
local numerical/WAV/state/UI/oracle tests, sanitizer results and native CI.
The new acceptance workflows include independent analytic tones, fast-playback
alias suppression, effective ratio cancellation, malformed/versioned state,
atomic grouped refusal, overflow, asset endpoints, immutable media, live/export
waveform equality, seek/split/crop, payload retirement and actual callback audits.
Existing full positioned corpora remain registered separately and unchanged.

Catalogs include new contextual keys. Draft translations/English fallback do not
establish native review or all-Europe language support. No local VM was started;
builds stay sequential at two low-priority jobs. Installer/source-pair refresh is
separate, avoiding large archive writes during backup contention. Existing preview
installers contain earlier code; no binary release upload is authorized/inferred.

## Next implementation

Implement required independent pitch and time stretching after the candidate
Rubber Band review: pin source/licenses, decide quality modes, channel/layout
behavior, latency/pad/tails and drain, exact duration/seek/split semantics, bounded
worker preparation and cancellation/recovery. Compare live/offline results,
transients/phase/alias points, native Windows state/UI and installed workflows.
Rate automation/tempo maps and reference-aligned full quality tests remain open.
Foreign-suite properties need typed conversion and loss previews; this rate
control does not establish native-project compatibility or promote a parity row.

## Observed validation repairs

The first29-test Release batch passed26 and failed three checks. The old
session-state future-schema refusal still used11, now the supported current
schema; it now uses12 and keeps the same UnsupportedSchema assertion. The
translation source inventory was stale after the focused Undo refresh repair;
all draft catalogs were regenerated. The new desktop workflow waited for the
controller's Ready snapshot but clicked Stop before its queued widget update;
it now also waits for the enabled Stop button, matching the existing workflow.
No engine or waveform assertion was removed. Raw draft logs remain historical,
without exact committed input/binary qualification. The focused no-fade rate
Undo case separately verifies the production refresh repair.

The first Debug O0 sanitizer batch passed10/11 but timed out in the focused
no-fade Undo case. A diagnostic reproduced stored5/4 with focused display2.000,
no controller error. Coalesced Apply/Undo can skip the intermediate GUI model,
so comparing only old/new model rates misses the stale field. The control now
retains the last displayed/admitted exact ratio and forces canonical refresh when
that differs from incoming state. Unrelated publications preserve unapplied input.
Unchanged Apply also retains an exact1/3 ratio despite its0.333 display. The
existing model/field equality assertion remains, with additional exact-ratio
Save/reopen coverage. The failed batch and diagnostic remain historical evidence;
final qualification must use the subsequent receipts.

## Final local qualification

[Exact input/binary receipt and raw logs](../tests/results/M2/2026-10-09-clip-playback-rate/)
record579 source/build/catalog hashes, seven Release executables and two sanitizer
executables. Release29/29 passes in36.39s. Debug O0 ASan/UBSan11/11 passes in148.04s,
with leak detection disabled and each numerical/WAV/rate family retaining its90s
process budget. No previous timeout or full native deadline is thereby closed.

The linked-rate family performs4655 checks and18 owned recorded-WAV projects.
The independent349-case Fraction oracle accepts221/refuses128 actual C++
positions. Selected1kHz tone maximum error is1.94677e-7;12kHz rejection at4x has
peak1.70386e-12. These selected points do not establish full Q-STRETCH/pitch/SRC
or reference parity. Callback allocation/free/locking audit counters remain0.

Unchanged positioned corpora retain750959 kernel/state and2216 owned-WAV checks,
323574 pinned aligned samples with maximum difference0, and755 independent
fraction probes. All34 draft catalogs pass structural checks with770 source keys,
3135 draft translations and0 native-reviewed/fully UI-qualified languages.
Desktop test check counts can differ with polling/screenshot execution; raw logs
record exact observed successful counts. Current exact-source native Windows CI,
installed/native audio, installer refresh and full F/Q/C/N remain separate gates.

## Initial native CI and UI synchronization

Run37961071889 at2e390e1 failed: Linux101/102, native Windows core36/36,
native Windows Qt9/10. Only desktop-playback-rate failed. Windows observed the
controller's5/4 ratio before the queued label update; Linux clicked Apply after
the stopped/edit snapshot but before the queued controls became enabled.
The revised test awaits both model and observable GUI readiness. It retains
the same assertions and deadlines; production implementation is unchanged.

A deterministic regression now admits2x in a focused control, skips the
intermediate GUI publication, then publishes canonical Undo5/4 and asserts1.25.
The three affected UI families pass Release3/3 and Debug O0 ASan/UBSan3/3
(leak detection off). The separate synchronized-ui-qualification.json binds
the revised test, executable hashes and raw logs. Initial failed CI archives
retain original ZIP digests, CRC checks, raw logs and exact counts. The original
29/29 and11/11 local receipt retains its original input scope. Revised native CI
must qualify the new head before merge; these local checks do not close that gate.

The review also identified combined rate/processing refresh flags discarding
unapplied focused input on an unrelated edit. The flags are now separate. New
regressions preserve focused1.7x input across processing-only publication and
focused3.5dB gain across rate-only publication while showing canonical2x speed.
The coalesced Undo regression remains. Release4/4 and Debug O0 ASan/UBSan4/4
pass across the full timeline and three affected families. The separate
separate-refresh-qualification.json binds the three revised source files,
579 total inputs, executables, raw logs and review evidence. Native CI must
qualify this subsequent production repair; earlier receipts retain their scopes.
