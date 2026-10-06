# ADR 046: saved declared input latency per track

Date: 2026-10-06. Status: accepted for the current audio recording slice.

## Decision

Store `Track::inputLatencyFrames` as an exact signed 64-bit whole sample count,
from zero through 60 seconds at the project rate. Schema 1.5 requires this field;
1.0–1.4 explicitly migrate to zero. A stable-ID `SetInputLatency` operation joins
transactional structural Undo/Redo. Strict decoding rejects floats, booleans,
strings, missing/unknown fields, overflow and out-of-range declarations. Opening
an older project never rewrites it automatically.

The recording inspector exposes an accessible integer control and locale-formatted
milliseconds readout. Unfocused wheels propagate scrolling. New takes use the
accepted preparation snapshot, including every armed lane; an admitted change
retires the incompatible prepared/active recording owner after join. The UI
prevents editing until Stop, finalized-take handoff and pending commands are done.

The setting declares total capture alignment in project samples. It does not
infer hardware latency, add an unmeasured device delay, change monitoring, shift
existing clips, process raw samples or change normal exports. Capture metadata
retains the original declaration. Ordinary attachment subtracts it once, trimming
source samples if the aligned start would precede zero. Musical punch captures
`[punchIn + delay, punchOut + delay)` and attaches at desired punch-in; the existing
checked postroll admission remains authoritative.

## Alternatives and consequences

A global setting loses separate lane alignment. Milliseconds as canonical state
introduce rounding and locale problems. Live mutation of an active capture offset
breaks its timing origin/journal and can misplace a take. Automatic measured-device
compensation and physical round-trip calibration remain separate required work.
The current zero default preserves every older project without a guessed delay.

Track state is Qt-free. Allocation, JSON, history and GUI formatting remain on
control/disk threads; no new callback synchronization or dependency is introduced.
Windows core cross-compilation does not establish native Windows recording/UI
qualification. Tempo, manual punch, Auto monitoring, loop/take lanes and comping
remain in the full frozen-reference scope.
