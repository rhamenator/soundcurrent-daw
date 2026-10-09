# Editable independent pitch/time-stretch state

2026-10-09. SC-DAW-BASELINE-2026-10-05 is unchanged. This checkpoint adds
framework-independent project state and checked artifact adoption to the
[isolated worker](134-isolated-stretch-worker.md). Desktop controls and process
supervision are the next implementation task; installed previews retain older code.

## Raw anchor, derived audio and editing

Schema 1.12 stores an optional per-clip stretch anchor. It identifies the immutable
raw asset and byte hash, exact physical-frame origin, selected raw span, canonical
rational duration, integer milli-cent pitch, formant option and processor/render
key. The clip's playback asset points to the verified floating derivative. Physical
sample rate, project sample rate and linked playback speed stay distinct.

A subsequent render reads the original raw span, including after split/crop. It
does not recursively stretch the previous derivative. Selected derivative offsets,
duration and fade anchors are retimed exactly. Sibling clips remain unchanged.
One structural edit attaches the new asset and state; Undo/Redo retains old and
new assets. No asset cleanup or cache eviction is implemented by this operation.

Adoption compares the expected clip and raw asset with the current project. Stale
clip/raw changes refuse atomically; unrelated track-name or EQ edits survive.
Legacy schema minors 0–11 decode without inventing an anchor. Unknown processors,
source algorithms, noncanonical fractions/settings, malformed types and future
schema minors refuse. Retained state and history include anchor payload costs.

Timing uses portable two-limb integer multiplication/division, signed floor/ceiling
and canonical fractions. Overflow or unrepresentable intermediate denominators
refuse explicitly; source positions never pass through floating-point rounding.

## Parent verification boundary

Protocol `sc-stretch-render-v2` adds exact fractional source origin and its source
algorithm to the stable content key. Equivalent fractions produce identical keys
and audio bytes. The worker uses the existing prepared positioned reader with
whole-file context and its declared boundary extension.

The parent codec bounds request/response JSON, rejects duplicate/nested fields and
verifies the actual ready identity. After child reaping, the artifact verifier opens
the fixed owned operation marker, hashes the original raw input and independently
checks every derived RF64 sample, original audio bytes, format, extent, peak and
receipt policy. A successful result owns its payload grant and yields a typed
structural edit. The verifier creates no directories and changes no project state.

Verification uses pinned media handles and cooperative cancellation/deadlines.
It is not a privilege sandbox or a hard deadline for blocked OS I/O. The desktop
supervisor must still admit the entire child ceiling before spawn, retain it through
reap, bound process channels and protect the project epoch/root before adoption.
Canceled or ambiguously terminated children require inspection of their owned
completion marker; absent completion never licenses attachment or automatic deletion.

## RF64 checking

The owned WAVE validator now supports bounded RF64 size tables and independently
decodes admitted PCM/IEEE samples from validated extents. It checks chunk padding,
table bounds, sentinel resolution, format/layout, sample count, finite samples,
original byte hash and unchanged file identity. This follows the size substitution
and first-size-chunk structure in [EBU Tech 3306, section 3.4 and Annex A](https://tech.ebu.ch/files/live/sites/tech/files/shared/tech/tech3306v1_0.pdf).

An independently constructed valid fixture exposed a libsndfile 1.2.2 issue with
odd-length ancillary RF64 chunks. The validator's bounded direct PCM/IEEE decoder
handles that fixture without altering the header or source. Other playback/import
consumers still use libsndfile; arbitrary foreign RF64 playback compatibility is
not established by this change. RIFF/RIFX validation retains its existing decoder.

## Evidence and required continuation

Initial local Release evidence: six selected test families pass. The actual worker
workflow completes 33 owned jobs, including 27 short-span boundary jobs, with 172
workflow checks and 1,043 shared live/export checks. Model tests report 61 checks;
an independent Python arbitrary-precision oracle checks 1,663 retiming cases
(1,262 accepted, 401 refused). Callback allocation/free/locking counters remain zero
in the completed-artifact reader workflow. These totals describe this initial run,
not native Windows or installed application qualification.

The [final local receipt and exact raw logs](../tests/results/M2/2026-10-09-editable-stretch/qualification.json)
bind 652 source hashes. The complete local Release configuration passes 110 tests
(including four synthetic PipeWire-adapter families absent from hosted CI).
Final corruption checks bring the shared reader/export total to 1,045; WAVE tests
report 20,956 checks, including RF64 PCM32 precision and nonfinite-sample refusal.
ASan/UBSan with leak detection pass the three state/arithmetic/WAVE families and
the actual artifact adoption/live/export workflow. That workflow uses a Release
worker and an instrumented parent artifact test: the worker's OS address-space
ceiling is incompatible with ASan's virtual mappings. No sanitized worker is claimed.

Protected native Windows qualification remains pending for this source. The frozen
F/Q/C/N acceptance gates stay open. Required work includes supervised desktop duration/pitch/formant controls,
recovery/cache lifecycle, very short clip context/alignment, dynamic warp maps,
segmented pitch editing, wider rates/layouts and independent transient/formant/
spatial processing-quality checks. No claim of reference parity follows from the
processor name or successful bounced-media playback.
