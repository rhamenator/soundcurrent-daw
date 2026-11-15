# Fractional comp input grid experiment

The production `SourceFrameMap::processingGrid` constructor represents a raw
origin and exact raw interval on an integer processing grid. It uses the existing
positioned FIR. The experiment investigates how that grid feeds the pinned R3
backend; it is not a production comp producer or project edit.

For raw interval `D`, processing frames `N`, and explicit output frames `O`:

- Input sample `i` addresses `rawOrigin + i * D/N`.
- The exclusive input endpoint is `rawOrigin + D` exactly.
- The backend duration ratio is `O/N`.
- With a grid source step `s=D/N`, the backend pitch factor is the requested
  factor divided by `s`. Resampling onto this grid otherwise changes pitch.

Rational coordinates stay exact. The final backend ratio and pitch factor are
floating DSP inputs, independently verified by duration and PCM observations.
Grid components must fit the existing 32-bit step representation and the kernel's
effective rate range of 1/192 through 192. Unrepresentable grids refuse explicitly.
The grid does not change a clip's saved playback speed, physical rate or anchor.

The probe uses owned stereo tones at 997/1703 Hz with float amplitude 2.5, a raw
origin containing 1/7 of a frame, and interval 8192 + 2/3 frames. It compares the
ceil grid of 8193 samples with a deliberately coarse grid of 9216 samples, both
rendered to exactly 12289 output frames. It tests octave-down, unchanged and
700.007-cent-up pitch with and without compensation. Every output PCM is retained.

Two additional renders compare a nominal 8192-input/12288-output artifact with a
whole rerender adding 26 output frames at each edge. Their middle samples are
compared directly. This evaluates whether a whole rerender can meet the existing
nominal/current PCM preservation contract.

The probe's 0.5% frequency refusal is a bounded feasibility guard, not an adopted
processing-quality target. `analyze.py` independently measures middle-window
spectral peaks and compares the original PCM. NumPy is used only for analysis.
Transient behavior, phase and seams, formants, fractional protected landmarks,
repeated extensions, native endpoints and full processing quality remain open.

Build the main repository's `sc-processing-grid-tests`,
`sc-positioned-resampling-tests`, `sc-comp-handle-plan-tests`, and `sc-rubberband`
targets in a dedicated Release build with media on and desktop/PipeWire off. Then
configure this directory with `SC_DAW_BUILD` pointing to that build. Use one local
build worker. `qualify.py` runs the focused tests, probe and independent analysis
against a clean committed source, checks unchanged source/binary hashes, and
retains its source/PCM/process receipt in a new evidence directory and archive.
