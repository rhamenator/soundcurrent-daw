# X004: import other suites' work files

Owner requirement added 2026-10-05: import other DAW suites' work/project files. This supplements P080/P081/P082/P091 and extends the native-project program beyond the two frozen references. It does not change those reference versions.

## Scope and priorities

Native project import is a required capability. Exchange formats are additional adapters with their own preservation limits. Opening stems or a DAWproject export alone does not establish native project import.

| Target family | Initial route | Qualification status |
|---|---|---|
| Bitwig Studio projects | Native `.bwproject` investigation plus separate DAWproject adapter | No native parser/corpus qualified; required investigation starts in M0 |
| Cubase projects | Native `.cpr` investigation plus separate DAWproject/AAF/OMF workflows | No native parser/corpus qualified; required investigation starts in M0 |
| REAPER projects | Native `.rpp` adapter candidate | Seven original7.82/Linux native-writer projects frozen; owned scalar/byte IR added; worker property preview, conversion and render compatibility unqualified |
| Ableton Live, FL Studio, Studio One, Pro Tools, Logic and other common suites | Inventory native containers, lawful documentation/fixtures and exchange options per suite | Required coverage program; exact version envelopes and parsers remain unqualified |
| Standard exchange | DAWproject, AAF/OMF, SMF, MusicXML; other formats after evaluation | Separate semantic coverage and loss reports per format |

This is a priority register, not a claim that each format is documented, legally redistributable, or already supported. Preserve the breadth while resolving feasibility rather than silently substituting media-only imports. Native version envelopes must be frozen per adapter independently of the product's reference baseline.

Primary evidence: [DAWproject specification/reference](https://github.com/bitwig/dawproject), [Cubase Pro15 AAF import](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/exchanging_files_with_other_applications/exchanging_files_with_other_applications_importing_aaf_files_t.html), and [REAPER official guide](https://dlz.reaper.fm/userguide/ReaperUserGuide774.pdf). The REAPER guide is a project-file overview, not a complete parser specification. Documentation gaps remain unknowns.

## Import contract

Run format sniffing/parsing on an isolated, cancellable worker with bounded file/container sizes, entry counts, nesting, decompression and opaque state. Never execute project-embedded scripts, launch referenced programs or load plugins merely to preview an import. Resolve external media only through explicit approved roots and content verification. Reject unsafe archive paths and preserve the input project unchanged.

Adapters produce an **import intermediate representation**, rather than force foreign state into today's one-track schema. It preserves foreign object identity, source format/version and opaque unsupported data. Map that representation into versioned SoundCurrent session objects as the necessary subsystems exist.

Inventory and convert:

- Tracks, groups, buses, sends, sidechains, channel layouts and gain/pan conventions.
- Audio clips, source extents, takes/comp edits, offsets, fades, stretching/pitch and media dependencies.
- MIDI notes, controller/expression data, tempo/meter maps, markers and musical versus sample timing.
- Automation curves, modes, interpolation, parameter namespaces and modulation where representable.
- Plugins/instruments, instance identity, state blobs, presets, external samples and missing-plugin placeholders.
- Launcher/scenes/containers, notation/articulations, immersive/video/timecode and other suite-specific objects.

Every object/property is reported as **preserved**, **converted**, **unsupported**, **missing**, or **unverified**. Conversion notes identify semantic changes; unknown data never disappears silently. Preview the report before committing a new SoundCurrent project. Persist provenance and the report alongside the imported project; opening a converted project must not require the original suite unless the report explicitly identifies an external dependency.

## Acceptance and task dependencies

1. M0: freeze per-adapter format versions, inspect lawful primary documentation and establish rights-cleared source projects. Make projects in source suites containing one representative instance of every claimed property and failure case.
2. M1/M2: provide a media-safe transactional destination and non-destructive clip model. Current snapshot schema alone cannot represent the required import breadth.
3. M3–M7/M9/M10: map timing/notes, routes/automation, plugin state, performance structures, notation and immersive/video data as their destination models become available. Preserve unsupported intermediate data meanwhile.
4. M8: deliver import UI, worker adapters, mapping and persistent per-object loss reports. Prioritize Bitwig/Cubase and a documented native-format feasibility adapter early; extend the suite register rather than defer everything to M11.
5. M11: compare source and imported project structure, media hashes, notes/tempo timing, automation, routes and available plugin states; compare aligned renders and complete editing/reopen workflows. Run corruption, missing-media/plugin, cancellation and migration cases on Linux and Windows.

Native compatibility is directional and versioned. A passing exchange conversion is not a passing native adapter. No native compatibility is currently claimed. Consequential rights/access gaps stay visible until resolved or explicitly accepted by the owner.

## Structural feasibility checkpoint (2026-10-08)

The original bounded RPP outline now preserves raw bytes and structural offsets
under shared resource admission and cancellation. Synthetic tests cover unknown
state and refusals; every semantic property remains unverified. No native writer
version/corpus or conversion is qualified yet. Later checkpoints below add worker
and desktop inspection evidence without promoting native compatibility.
See [checkpoint 113](113-rpp-structural-inspection.md) and
[ADR081](decisions/081-bounded-foreign-project-outline.md). Native imports for the
other registered suites remain required.

The [separate inspection worker](114-import-inspection-worker.md) adds pinned
read-only file loading, a bounded versioned source-hash/range report and actual
child-process cancellation/refusal tests. The [desktop inspection preview](115-desktop-import-inspection.md)
adds shared admission, strict report validation, async retirement and a read-only
outline. Persistent opaque state, OS sandboxing, native version/corpus, semantic
mapping and Windows Qt/installed qualification remain open.

## Portable inspection checkpoint (2026-10-08, later)

[Checkpoint 116](116-inspection-bundles.md) retains exact source/protocol bytes
in a versioned `.scinspect` file. Save/Open run on the existing admitted I/O
controller, preserve existing destinations, and keep every property unverified.
Reopen can succeed with the original source absent and no child process. This
is persistence of inspection evidence; the import IR, source-writer/version
corpus, semantic mapping, approved conversion and full adapter program remain
required. Windows native/Qt/filesystem/installed qualification is tracked
separately. Existing preview installers are unchanged.

## Known-writer corpus checkpoint (2026-10-09 UTC)

[Checkpoint118](118-native-writer-import-corpus.md) adds original projects actually
saved/reopened by REAPER7.82/Linux through public APIs, matching property witnesses
and exact hashes. The real inspector preserves complete bytes and unverified
TRACK/ITEM inventories; it does not resolve media, convert properties or qualify
native compatibility. Windows inspection of the Linux-generated corpus is a
separate gate from a Windows writer. The next task is the admitted import IR and
per-property mapping/loss report, followed by approved conversion and independent
render comparisons. All other registered suites/formats remain required.

## Source-property IR checkpoint (2026-10-09 UTC, later)

[Checkpoint119](119-import-intermediate-properties.md) adds an original admitted
C++ model with stable field IDs, source-side scalar/byte values, exact opaque state
and preserved/unsupported/missing/unverified records. Tests compare seven actual
writer witnesses and exercise malformed/duplicate/missing/ambiguous state and
retirement. No field is converted and no media is resolved. Property worker/report,
parent validation/UI, approved destination mapping and aligned renders follow;
the installed preview and complete adapter/parity scope remain unchanged.

## Source-property preview checkpoint (2026-10-09 UTC)

[Checkpoint120](120-import-property-preview.md) connects the owned model to a
versioned isolated worker report and independently admitted parent validator.
Read-only Properties/Original source tabs show original values/units and losses;
Save/Open retains them and continues to support older outline-only inspections.
No media/plugin resolution or destination conversion occurs. Local Linux and
MinGW evidence is separate from new native MSVC/Windows Qt qualification. Next:
approved media roots/missing choices, explicit new-project mapping, Undo/reopen
and aligned source/destination renders. Full registered import/parity scope remains.

## Approved-media admission foundation (2026-10-09)

[Checkpoint122](122-approved-media-roots.md) adds explicit directory capabilities,
relative pinned plain-file reads and bounded checksums. Owned Linux tests cover
Unicode/links/FIFO/root replacement/quotas/mutation/cancellation; native Windows
execution is pending separately. The developer probe matches two original corpus
media hashes without decoding audio. The desktop inspector still performs no media
access. Next: explicit root/replacement choices, pinned WAV validation/copy and
new-project mapping with loss/Undo/reopen/aligned-render evidence. The full native
and exchange family register and all frozen parity axes remain unpromoted.

## Pinned WAVE content increment (2026-10-09)

[Checkpoint123](123-pinned-wave-validation.md) adds original strict WAVE preflight
and existing libsndfile virtual I/O over the approved file object. Complete finite
sample decoding preserves float headroom, source rate/count/mask/precision and byte
hash. No filename is reopened, reference occurrence selected, correction printed,
media copied or destination state changed. Linux independent sample/real-child
gates pass; native Windows follows independently. Remaining WAVE/other formats
are explicit gaps. Next: parent report validation, explicit roots/replacements and
the cancellable media checklist, then the complete conversion/loss/render chain.

## Explicit desktop media choices (2026-10-09)

[Checkpoint124](124-desktop-media-checklist.md) connects the pinned checker to a
read-only virtual media table. Explicit folders/replacements, independent v2
report/PID validation, cancellation/deadlines and terminal child retirement are
implemented. Choices are local to the window and excluded from saved inspections;
missing/duplicate/unsupported evidence and original references survive. Linux
checks are distinct from new native Windows/installed gates. Next: verified
transactional copying/new-project semantic state, loss preview, Undo/reopen and
aligned renders. All registered formats and frozen parity contracts remain open.

## Bound staging publication checkpoint (2026-10-09)

[Checkpoint126](126-media-provenance-transaction.md) records exact original
occurrence/selection evidence, planned intent, exclusive verified media receipt
and source-free full decoded/hash recovery. Original known-writer files remain
unchanged. This is media publication, not project/session semantic conversion,
multi-file atomicity, Undo/render equivalence or full native compatibility.
Desktop copy/recovery, destination semantic state, loss review, installed previews
and every registered native/exchange adapter remain required.

## Desktop checked-copy checkpoint

[Checkpoint127](127-desktop-checked-media-copy.md) connects frozen checked rows to
owned copy children and explicit recovery. Changed snapshots refuse before writes;
stopped/unverified children retain uncertain outcomes. This preserves media and
provenance without Session conversion. Next extend/persist original loss state and
destination timing/gain/fades/rate/pitch, then preview/Undo/reopen/aligned renders.
All registered adapters and native compatibility remain required and incomplete.
