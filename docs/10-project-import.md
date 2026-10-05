# X004: import other suites' work files

Owner requirement added 2026-10-05: import other DAW suites' work/project files. This supplements P080/P081/P082/P091 and extends the native-project program beyond the two frozen references. It does not change those reference versions.

## Scope and priorities

Native project import is a required capability. Exchange formats are additional adapters with their own preservation limits. Opening stems or a DAWproject export alone does not establish native project import.

| Target family | Initial route | Qualification status |
|---|---|---|
| Bitwig Studio projects | Native `.bwproject` investigation plus separate DAWproject adapter | No native parser/corpus qualified; required investigation starts in M0 |
| Cubase projects | Native `.cpr` investigation plus separate DAWproject/AAF/OMF workflows | No native parser/corpus qualified; required investigation starts in M0 |
| REAPER projects | Native `.rpp` adapter candidate | Official guide identifies RPP as text; that does not prove complete field semantics |
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
