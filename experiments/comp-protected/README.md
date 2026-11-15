# Protected comp-handle feasibility

This opt-in executable tests an adapter from an original protected comp envelope
to the existing in-process protected-warp renderer. It has no install rule,
application hook, owned-worker protocol or project mutation. The shipping comp
producer still refuses nonuniform maps.

## Reproduce

From the repository root, with the usual C++ and media build dependencies:

```sh
cmake -S . -B build-comp-protected -DCMAKE_BUILD_TYPE=Release -DSC_BUILD_DESKTOP=OFF -DSC_BUILD_PIPEWIRE=OFF -DSC_BUILD_COMP_PROTECTED_PROBE=ON
cmake --build build-comp-protected --target sc-comp-protected-probe --parallel 1
python3 experiments/comp-protected/qualify.py build-comp-protected/sc-comp-protected-probe comp-protected-evidence
```

The evidence directory must be new. The driver bounds the child to 180 seconds,
retains stdout/stderr on failure, binds source/executable hashes, and independently
checks geometry using Python `Fraction` and PCM using exact little-endian bytes.
The CMake option defaults OFF. No test is silently added to the standard CI inventory.

## Candidate contract

Rebase the original user anchors into the expanded actual-source region and shift
their output coordinates by the handle prefix. Preserve anchor identity and the
protection/halo policy. Compare both maps at the union of their knots before
reading samples; equality at the user anchor alone would not detect a changed
protected boundary. The reader translates relative positions to the exact raw
origin once. The renderer preserves its existing gap processing and halo policy.

The candidate admits only an original protected clip with integer acoustic
positions, unchanged raw/output sample clocks, zero pitch, preserved formants,
and no extra processing context. History regeneration and previous comp handle
artifacts are explicitly refused. Fractional origins/endpoints are not rounded.
This first bank uses one marked attack with a sustained bed and two delayed,
polarity-related channels; it is not general microphone-group qualification.

At each of 44.1, 48 and 96 kHz, render a nominal control and two expanded handle
regions: nine renders, six with new handles. Verify exact duration, finite output,
headroom above unity, full protected-core sample identity, resource release,
cancellation/budget refusal and both fractional-origin refusals. The oracle also
checks the expected source and output extents independently of the C++ report.

General audio quality outside the copied core, multiple anchors, fractional grids,
post-SRC clocks, repeated history, owned process limits/publication, UI adoption,
Windows runtime and installed delivery remain open. This executable is a
feasibility test and must not be advertised as a supported production feature.
