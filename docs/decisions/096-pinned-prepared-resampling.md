# ADR096: pinned prepared resampling and rational timing

Status: selected for the prepared component; Session/native/quality integration
remains separately qualified. Date: 2026-10-09.

| Alternative | Evaluation | Decision |
|---|---|---|
| Treat source and project frames as interchangeable | Wrong duration/positions at different rates; fractional splits lose phase | Refused |
| Linear interpolation as the professional conversion default | Simple integration but no demonstrated stop-band/quality parity | Refused |
| Pinned libsamplerate0.2.2 best-sinc | BSD-2-Clause compiled kernel; separate unused helper notices retained; Linux CMake/C99; MSVC/MinGW qualification; expensive per-channel private buffers and end-condition adaptation | Selected for this component |
| soxr0.1.3 | LGPL-2.1-or-later, resampling/FFT phase/delay; older maintenance snapshot; Windows/build/transitive quality gates | Candidate; not adopted |
| Rubber Band4.0.0 | GPL-2.0-or-later/alternate commercial; independent pitch/stretch, pad/delay/drain and rate-range decisions | Candidate for the required independent pitch/stretch slice |

[Checkpoint130](../130-prepared-resampling.md) links official primary license/API
sources, exact pins, source hashes, configured channel bound, failure evidence,
resource ownership, timing and exit criteria. Original dependency source remains
unmodified; scoped original build adaptation is auditable. No system installation.

Use an exact rational source/project map; retain fractional origins under advance.
Use one serialized prepared worker API shared by future playback/export callers.
Explicit rational end plus bounded virtual context resolves the actually observed
mono/multichannel count disagreement without suppressing the assertion. Equal-rate
samples bypass filtering. Finite headroom remains internal; non-finite output
latches failure. Declare source context and distinguish logical alignment from
live input availability/PDC. Release the lease and state off RT.

Component tests and narrow pass-band/alias points do not establish full Q-RESAMPLE
or native-project compatibility. No current Session conversion, arbitrary imported
fractional-origin support, live callback API, state/timing migration, pitch/stretch,
UI/installed workflow or full European qualification is inferred. Those remain
explicit work toward the frozen full product goal.

The complete-source inventory distinguishes the compiled BSD-2-Clause kernel
from unused Autoconf macros under GPL-3.0-or-later / GPL-2.0-or-later with their
file-specific configure-output exceptions, FSFAP notices, a historical permission
notice, and the BSD-2-Clause FFTW discovery helper. Preserve all original notices
and exceptions. The GPL-2.0-or-later macro is distributed under its later GPL-3.0
option, with the text in the root LICENSE. These unused helpers introduce no
selected binary dependency. See `additional_source_notices` in the manifest.
