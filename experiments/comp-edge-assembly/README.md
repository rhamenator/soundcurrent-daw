# Separate edge rendering and bounded assembly

This experiment renders a nominal artifact, a prefix with real surrounding raw
context, and a suffix with its own context. It crops 26 output frames from each
edge job, then uses the production `assembleCompHandlePcm` primitive to emit those
edges around the untouched nominal buffer. It never mutates or admits a project.

Twelve stereo cases cover duration multipliers 1/2 and 3/2, independent pitch
of −1200, 0 and +700.007 cents, and steady-tone/transient-train signals. All raw
signals are owned and preserve floating headroom. The raw origin has a 1/7-frame
fraction; the processing grid retains exact fractional context endpoints and
compensates the grid's pitch factor. All context samples come from real raw data.
Each case records exact grid/origin/crop recipes and all nominal, context and
assembled PCM. Two assembly block sizes must produce identical output.

The new assembly library streams bounded slabs and retains ledger credit for its
scratch buffers. It reports each boundary's actual step and deviation from the
neighboring first differences. These are measurements, not a quality admission
threshold. It neither modifies the preserved inner samples nor repairs a poor
join. The caller must independently verify/pin sources and own publication,
deadline/cancellation and partial-output recovery. The callbacks run off audio.

`analyze.py` independently compares complete PCM bytes, recomputes frame/crop
recipes with Fraction, and measures the joins from the assembled samples. NumPy
is only an analysis dependency. Expect unresolved joins to remain visible rather
than interpreting PCM preservation as a sonic-quality result.

Build `sc-comp-handle-pcm-tests`, `sc-processing-grid-tests` and `sc-rubberband`
in a dedicated DAW Release tree (desktop/PipeWire off, media on). Configure this
directory with `SC_DAW_BUILD` pointing to that tree and use one low-priority local
build worker. `qualify.py` runs the core tests, actual probe and independent
analysis against a clean committed source and retains source/binary/process
receipts with all owned PCM in a new archive. Production edge job ownership,
general protected/context maps, formants, quality admission, controller and UI
integration are separate required work.
