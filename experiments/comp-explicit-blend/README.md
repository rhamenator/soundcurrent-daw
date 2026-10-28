# Explicit comp processing-region experiment

The separate `blendCompHandlePcm` API implements the proposal in ADR157. It uses
a complementary quintic curve with one weight for all channels, solely inside
explicit permitted inner prefix/suffix regions. Current samples outside those
regions, generated outer edges, original assets and the existing strict assembly
path stay unchanged. No existing model verifier or schema is relaxed.

The probe reads checkpoint268's immutable owned PCM bank, including all 36 actual
R3 nominal/context renders. It performs 36 new assemblies (12 cases × widths
26/128/1024), with no new R3 render. It compares 127/511-frame assembly partitions.
Permissions and curve identity are retained with each output.

The independent NumPy analysis compares whole PCM with the explicit curve,
checks all outside bytes and edges exactly, recomputes join measurements, and
records same-window high-frequency energy and local RMS tradeoffs against the
hard splice. These metrics remain diagnostic; there is no sonic-quality verdict.

Configure this directory with `SC_DAW_BUILD` pointing to a dedicated current core
Release build. Build `sc-comp-handle-pcm-tests` and `sc-comp-handle-blend-tests`
there. A separate desktop build qualifies the corrected import UI timing test.
`qualify.py` verifies every retained input against checkpoint268's source-bound
archive, runs both core gates and the desktop gate, processes/analyzes the bank,
checks unchanged source/binaries/inputs, and archives a self-contained PCM receipt.
Use one local low-priority worker. No native endpoint, project adoption or VM.

Production permissions still need exact project-to-physical derivation, resampler
neighborhood treatment, portable job/state provenance, independent artifact review
and full quality acceptance before integration. The current prototype consumes
declared permissions; it does not authenticate the requested project edit itself.
