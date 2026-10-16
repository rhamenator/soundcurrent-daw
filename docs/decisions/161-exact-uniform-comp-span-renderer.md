# ADR 161: exact-grid uniform comp span renderer

Status: accepted rendering primitive; general owned producer/state/quality open.

Derive raw span and physical output duration from immutable envelope maps. Verify
uniform slope at every interval, retain fractional endpoints, prepare an exact
processing grid and compensate independent pitch by its raw step. Preserve clip
playback speed and nominal anchors. Use the existing positioned kernel and pinned
R3 offline backend; copy only exact unity geometry with zero pitch.

Keep explicit allocations/I/O/study/process/drain off audio and GUI. Admit owned
slabs and require the later worker's process ceiling for third-party allocations.
Refuse short, incomplete, nonfinite, unrepresentable and nonuniform inputs without
silent padding, flattening or timing repair. Nonuniform production remains required.

Separate duration/grid/fundamental diagnostics from waveform/formant/phase quality.
Retain failed observations and strong off-fundamental spectral peaks. No library
report alone authorizes adoption: owned recipe/binary/job completion, source/media,
curve/alignment, post-SRC representation and full quality gates are still needed.
