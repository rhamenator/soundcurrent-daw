# Simulated native Stop delivery and Qt macro boundary

2026-10-11. Full professional-suite scope remains active and incomplete.

## Reproduced completion cycles

The simulated SDK uses one service thread for capture and render. Actual WASAPI
owns independent SDK threads. Two fixture waits could withhold an event needed by
the other role: waiting for every source copy while a full output bank needed a
render lease, and waiting for a full render lease while capture's original Stop/
end was needed to certify a short final tail.

A deterministic regression forces capture end to arrive during a short render
tail. Its output bank has 66 slots: the admitted minimum for the 4,097-frame native
capacity plus one 64-frame processing grid. Before correction, the original
five-second wait expired with processing/disk/capture/render all unfinished,
8,192 frames published, 8,160 delivered, Stop planned and 32 tail frames waiting.
Those observations were taken before cancellation. The test's original source,
commands, failure and bindings are retained.

The fixture now permits a ready full render lease to release output pressure
while capture waits for source copying. While render waits, it dispatches capture's
actual lifecycle Stop/end callback. It does not invent EOF, normalize SDK results
or certify cancelled work as completion. Virtual ten-millisecond packet cadence,
original five-second completion limit, native algorithms and production ownership
code remain unchanged. The full suite preserves original cases and adds this
forced schedule, exact raw bits, finite 8,192-frame end, applied EQ, independent
reopen and original Stop-result verification.

Early experiments are retained separately: deferring capture end too long caused
a legitimate clock-age failure; 65 output slots were rejected by the existing
native-capacity-plus-grid admission; the admitted geometry exposed the separate
source-copy/render pressure cycle. Their failures are not reported as passing.

This establishes fixture scheduling defects. PR152's precise stalled owner remains
unproven because its original pre-cancel state was not retained. Later passing
native runs do not establish a causal correction of that historical failure.

## Bounded observations for future failures

Native completion failures now log original owner progress before cancellation.
When explicitly configured by CI, the joined test retains only `observed.json`,
byte-exact `native-run.json` and `journal.json`: at most 4 MiB per metadata file and
8 MiB total. Audio and binary stamp/EQ data are excluded. The forced successful
case exercises retention and validates original completed state in every profile.

In JSON, `observedBeforeCancellation` describes the retained owner-progress
snapshot; SDK tail counters are copied after joins. The pre-cancel stderr record
also includes contemporaneous tail counters. These are independent atomic
observations, not a manufactured coherent device timestamp or project alignment.

## Qt build correction

PR153 exposed the same macro collision on Linux and Windows when localization
included Qt before the studio header. Header order alone was insufficient.
All GUI libraries now export `QT_NO_KEYWORDS`; affected signal calls use `Q_EMIT`.
Engine identifiers and project schemas stay framework-independent. This follows
[Qt's documented third-party integration option](https://doc.qt.io/qt-6/signalsandslots.html#using-qt-with-3rd-party-signals-and-slots).
No Qt dependency upgrade or new library is introduced.

## Qualified evidence

[Local packet](../tests/results/M2/2026-10-11-native-render-tail-stop/summary.json)
binds implementation `d0c1340`, 784 source/checkout inputs, original experiments,
generated commands/configurations, object/archive/executable hashes, logs and
bounded owner metadata. Unchanged qualified engine/native archives and test objects
are verified read-only. Three current native objects and a private controller
archive are built per profile.

The full native suite passes Release 19.59 s, ASan/UBSan 27.04 s and TSan 30.74 s,
including forced Stop delivery, existing controller/admission/failure/pressure and
raw-bit workflows. Guarded callback allocations/frees/locks remain zero. One
verification-script refusal expected a CLI-only trace in the full suite; the actual
Release suite had passed and was retained without rerunning. Verification uses the
executed full-suite markers and original forced-case metadata.

The corrected GUI policy compiles 31 current objects and private GUI libraries.
Release localization, actual main-window panel, loop controls and desktop UI pass
in 13.69 s. Catalog/refusal audits pass; 34 catalogs/947 keys/3,135 drafts/zero native
reviews/UI qualifications remain. Current Windows execution and GUI sanitizer
qualification remain open. Prior system Qt/D-Bus TSan reports remain unqualified.
No physical endpoint, default native factory or installed preview is qualified.

## Original PR153 results

[Original four-job evidence](../tests/results/M2/2026-10-11-native-window-qt-compile-failure/summary.json)
retains run `38197361681`, head `a32416f3f9ec87190d79b435f75bd7843bcb5ca9`.
The run is terminal failed: native Windows passes 71 suites in 207.94 s, the original
manual cases and three standalone diagnostics complete, and the core cross-build
passes. Linux and Windows desktop fail compiling localization because of Qt's
`slots` macro. Desktop runtime did not execute. All original logs, artifact log/JSON
members, metadata and exact tested-merge bindings are retained. This predates both
corrections; no unchanged retry or all-green claim is made.

## Next work

Qualify the corrected source in Windows CI, then add worker-owned device inventory
and explicit native selection to the main-window workflow. Aggregate retained
memory, GUI framework retirement, Linux native integration, alignment,
installed/reopen/export and all later parity, import/profile and European language
gates remain part of the active full goal.
