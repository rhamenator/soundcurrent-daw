# Verified comp fragment assembly

Checkpoint 278 adds `streamCompPairFragment`, a framework-independent disk/control
operation. It joins actual independently verified span-worker output to the
current fragment. It returns a candidate report; final publication, model adoption
and main-window integration remain separate required work.

## Binding and timing

The operation requires the current project to retain the selection's exact
operation/digest reference. It checks the supplied model and root/epoch context,
then independently inspects both owned children. Every required child must be complete before a
sink receives any samples, even when only one fragment is being streamed.
The selected span must match its original request, physical rate, layout and
expanded frame extent. The existing verifier binds intent, completion, source
files, actual output and executable identity.

Physical artifacts use the existing source-rate PCM blend. Project-after-SRC
fragments independently resample the current clip and expanded candidate using
their exact positioned maps, then blend in project frames. Both paths derive their
permissions and geometry from the immutable processing plan. They retain floating
headroom and copy unblended samples without level normalization or clipping.
Project-after-SRC output is explicitly project-rate media; this operation does not
mislabel it as a same-rate handle window.

Reads use the bounded, hash-verified media cache. Explicit source/blend slabs and
retained report metadata are charged to the supplied resource ledger. PCM byte
limits are checked before output; cancellation is checked between operations and
slabs. The deadline is cooperative around I/O and verification calls, not a hard
watchdog. The caller owns and budgets an unpublished sink. Exceptions and late
verification failures may leave partial sink bytes. They must never be adopted.

Both children, their sources and completion records are inspected again after
streaming. The operation rechecks model/context/reference before returning.
Concurrent model mutation is excluded by its serialized immutable-input contract;
final adoption still requires a fresh canonical-controller guard. No Qt, audio
callback, project edit, automatic file publication or process launch is added.

## Actual Linux acceptance

[The source-bound receipt](../tests/results/M2/2026-10-14-comp-pair-assembly/index.json)
binds implementation `fd71a6bd7896306aa1b4349125e9f65f7cbecacb`, tracked source,
all relevant libraries and four executables. CTest process 3883464 passes in
6.80 seconds with all inputs unchanged before and after. The preceding development
run also passes in 6.54 seconds; its original local evidence remains retained.

Six actual isolated workers complete stereo spans at source rates 44.1, 48 and
96 kHz, for a 48 kHz project, with independent pitch +700.007 cents and a 3/2
stretch. The corpus assembles two physical fragments and four project-after-SRC
fragments, including fractional source origin and independent playback speed.
Nominal current media is a controlled analytic fixture, not the output of an
independently qualified original nominal producer.

The oracle independently decodes the actual files and computes the quintic blend
with an expanded long-double polynomial. All 96 blend samples match the expected
rounded float in this corpus. All 202,620 unblended assembly samples match bits.
The separate media-review gate compares 98,272 physical and 202,092 project
samples outside their permitted edits exactly. Every output retains a peak of
at least 2.5. The project-rate oracle uses the existing positioned FIR with a
different 97-frame partition: this checks map/offset/chunk invariance, not an
independent FIR-quality qualification.

Failure tests cover an absent or only partially completed pair, stale epoch,
changed clip, missing trusted reference, invalid fragment, insufficient output
or memory grants, precancellation, cancellation after the first emitted slab,
sink failure, and actual sibling completion-record corruption during streaming.
Failed calls release their credits; no failed sink is published. The canonical
project and saved project remain unchanged. All six children are reaped.

The archive retains actual WAV inputs/candidates/assembled outputs, project
state, requests/selections/control/completion records, process logs and receipts:
4,384,901 bytes, 143 entries, SHA-256
`7b8a05d2c5c7af2ac41d6aff34bdd6213e921ec12f3f04661915748a817dfdef`.

## Remaining work

Production immutable output publication and recovery still need an owned final
assembly job/receipt, original nominal-producer provenance, independent alignment
and sound-quality evidence, and atomic paired adoption. Post-SRC results require
shared live/offline prepared overlay state, persistence and Undo/Redo; no existing
same-rate window is relaxed. Nonuniform/protected span production remains required.
Broader layouts, rates, containers, grouped edits and native Windows execution
of this new assembly path remain open. Existing spectral and mixed-join artifacts
remain recorded and unwaived.

The original predecessor CI is being retained separately before updating its PR
head. Its desktop/native jobs have passed; the full original run and bundle audit
must be terminal before the next push. This new path has a native CI test and
artifact collection configured, but no Windows execution is claimed yet.

No production UI files, audio routes, installed previews, dependencies, VMs or
equalizer repositories changed. Localization remains 1,038 keys, 34 partial
catalogs, 3,168 drafts, zero reviewed and zero UI-qualified languages. The full
frozen professional DAW goal remains active and incomplete.
