# X004: frozen original native-writer project corpus

Date: 2026-10-09 UTC. Product baseline SC-DAW-BASELINE-2026-10-05 unchanged.

Seven original projects now have actual **REAPER7.82/linux-x86_64** writer
provenance. They are saved/reopened through public APIs, with matching native
property readbacks and exact project/media hashes. This establishes an initial
native-writer corpus, not semantic import or a supported-version envelope.
The [fixture register](../tests/fixtures/reaper-7.82/README.md) records each case,
runtime hashes, original GPL authoring code/content and reproduction procedure.

The corpus covers an empty48k project; mono/stereo audio; item/source offsets;
explicit fades; track/item/take gain and pan; pitch/rate; Unicode/quoted names;
two tracks/send/opaque extension state; original MIDI notes and a tempo/meter
marker. No third-party sample, instrument, effect algorithm, font/theme or
runtime is part of the corpus. The authoring tool remains a separately licensed
proprietary evaluation/runtime; it is not linked into or shipped with the DAW.
See [ADR084](decisions/084-native-writer-corpus.md).

## Bounded authoring evidence

The private official archive/executable/libSwell/EULA hashes are recorded.
Original PCM is authored outside the writer and never played. Stock public APIs
perform project creation, save, reopen and readbacks. No executable internals,
proprietary implementation or algorithm were examined/copied. APIs and ordinary
owned project output are the investigation interface.

The authoring environment has a new writable lab, read-only runtime/system
libraries, private PID/mount/network namespaces and no host display, sound device
or server sockets. The first virtual-display probe could not connect; the actual
writer subsequently runs without a display. No GUI workflow is inferred.
Owned termination after the complete receipt returns signal15, which is recorded
separately from supervisor success. No normal GUI shutdown is claimed.

A cold reproduction initially hits an8MiB per-file cap while extracting the
default theme. The already-completed private profile shows25,136,322 bytes for
that theme. A32MiB cap allows the separate fresh reproduction to complete in
7.970928629 seconds, with all seven save/reopen witnesses matching the frozen
properties apart from new track GUIDs. Initial failure and follow-up receipts are
retained; vendor profile assets remain private. This is a trusted authoring lab,
not qualification of the product's hard process/RSS sandbox.

## Inspection acceptance

The real C++ worker accepts each frozen native file under a4MiB declared payload
budget. Temporary input names exercise Unicode paths; referenced media is absent
and never resolved. Its exact source SHA/byte inventory is complete, source files
remain unchanged and TRACK/ITEM block counts agree with the native API witnesses.
Every node/property remains unverified in the protocol. The original synthetic
corruption/resource/cancellation tests stay separate.

Linux Release and ASan/UBSan acceptance each pass6,013 checks (leak detection
disabled). Git-index hashes confirm every registered native/source/media blob;
scoped attributes preserve original project CRLF. The first hosted Windows run
refused the frozen `generate.lua` hash because its default text checkout became
CRLF. An explicit Lua LF attribute fixes source checkout without changing any
frozen bytes or weakening the hash oracle. A simulated CRLF-default checkout
checks every manifest file before the corrected hosted run. Source/binary/test/authoring receipts are stored
under `tests/results/X004/2026-10-09-native-writer-corpus`. Hosted MSVC inspection
of these **Linux-generated** bytes is pending. This does not exercise a Windows
source writer, Qt/bundle workflow, media conversion, aligned rendering, complex
plugin state, complete RPP grammar or the complete native compatibility program.
Native Bitwig/Cubase and the other registered native/exchange adapters remain
required. No local VM, user audio route or installed preview was changed.

PR60's separate exact head `d1eafaeb` passed75 Linux tests and twelve selected
MSVC tests of42 configured, including256 recording checks and complete writer
phase pairs. Its receipts/artifacts are retained. The47.8653604-second producer
wall duration is a synthetic fixture observation, not native timing qualification
or an explanation of the original queue-full failure. PR60 merged through
required checks at `40926f1`.

## Next implementation task

Add an original framework-independent, resource-admitted import intermediate
representation. Start property mapping against these known-writer audio cases:
project sample rate, track identity/name/gain/pan and audio-item position, source
offset/length, fades, source reference and layered gain. Preserve exact source
and opaque byte ranges; attach per-property preserved/converted/unsupported/
missing/unverified evidence. Keep rate/pitch, MIDI/tempo, sends and unknown data
explicit until their actual destination/processing workflows are qualified.

Parse foreign semantics in the isolated worker with bounded token/range/count
admission, cancellation and a versioned independently checked report. Do not
resolve media/plugins or mutate the session just to preview. Follow with approved
media roots, explicit new-project conversion, Undo/reopen and independently aligned
render comparisons on Linux and Windows. Extend the corpus as properties and
other source suites are implemented; no difficult family is removed from scope.
