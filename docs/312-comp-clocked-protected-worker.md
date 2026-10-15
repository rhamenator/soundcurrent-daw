# Owned worker requests for clocked protected comp rendering

2026-10-15. Full DAW goal remains incomplete.

## Implemented workflow

The explicit `encodeClockedProtectedCompSpanRequest` entry point selects
`sc-comp-span-protected-clocked-v1` and the matching clocked processor identity.
Existing uniform and protected encoders keep their original protocols and DSP.
The request payload now supports the owned original-geometry plan from checkpoint
311. The worker renders through that plan and writes float RF64 at its output rate.

The trusted request digest binds the exact recipe bytes, parent executor, worker
image hash, fragment and operation. Decoding rebuilds the plan from that recipe;
it does not accept submitted geometry as authoritative. The geometry digest covers
raw identity/hash/frame count/layout/rate, exact origin and extents, all map knots,
semantic anchor owners and protected spans, protection policy, chunk size and DSP,
map and resampling algorithm identities. A separate exact clock binding includes
origin, step, input/output rates, baked pitch and the history requirement derived
from the recipe. Ready and completion packets retain both bindings.

The existing process memory ceiling and watchdog cover the new renderer. Before
readiness and again before completion, the worker verifies admitted media and
retained history for both recipe sides. Changed media/history, malformed bindings,
expired deadlines and cancellation leave no accepted completion. Verification
checks RF64 bytes, sample hashes, frame/rate/channel identity and floating-point
peak. Consumers must still call history verification before using a retained span;
file integrity alone is not permanent history authority.

The pair-selection and editor entry points still use their existing encoders. No
new pair adoption or installed capability is claimed by this worker integration.

## Local acceptance

At clean implementation `9b265efcd5cbb484a0c8a4e4f88d6c14da670633`:

- Four suites pass in Release (53.66 s) and Debug
  (184.39 s), covering both new banks and the existing uniform
  and protected worker banks. Every original individual deadline remains in force.
- Each build checks eight successful new workers and 82 required refusals, with
  1,385,714 complete PCM samples compared against in-process recomputation.
- Independent RF64 inspection verifies the eight completion file/sample hashes,
  frame/channel counts and peak measurements. All eight output files are identical
  between Release and Debug. Six existing protected output hashes match their
  original qualified corpus; the six uniform worker successes also pass.
- The history bank creates real nominal renders, paired spans, published assembly
  and adopted state before submitting the new requests. Early/late history changes,
  cancellation and a contradictory history requirement are refused. Original input
  files are unchanged. The new outputs themselves are not yet adopted as a pair.
- Two changed C++ translation units pass MinGW syntax. Native execution of this
  new worker path is pending. No sanitizer worker-process claim is made here.

[Portable local evidence](../tests/results/M2/2026-10-15-comp-clocked-protected-worker/index.json)
retains commands, source/binary snapshots, logs, fixtures, successful and failed
runs, original WAVs and independent audits.


The first direct fixture used zero as a supposedly invalid clock origin. Zero was
valid, so the mutation was corrected; active-child checking was limited to the
actual owned request being observed. A 96 kHz fixture then reached the unchanged
minimum analysis-window refusal. Longer original gaps exercise valid rendering;
the short fixture and failure remain retained. The first history driver reserialized
a valid request and thus changed its byte digest. Sending the original request
bytes fixes that harness mismatch without relaxing request/ready verification.

## Preceding native adapter evidence

The preceding adapter run [38708881154](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38708881154)
is still running at `37f390824adfe3a35a0047847228be13a35f86cd` as this local checkpoint
is recorded. Its terminal results, original logs and artifacts must be retained
before advancing the draft PR. No native pass for either the preceding adapter
or the new worker is inferred from compilation. The existing draft PR remains at
that preceding head while this worker checkpoint is backed up separately.


## Next task

Expose an explicit versioned clocked-protected option in paired selection and
recovery, carry its request identity into assembly/publication/history records,
and verify it before adoption. Preserve legacy processor selection for retained
projects. Add real post-SRC ancestry and nonunity baked-pitch worker cases, mixed
pairs, stale/changed history during adoption, Undo/Redo, save/reopen and shared
live/export evidence, followed by Linux/Windows desktop workflows. The current
worker history test uses an actual published physical ancestor at 48 kHz; it does
not establish the full matrix of cropped, mixed and baked-pitch histories.

Processing quality, broad material/layout coverage, perceptual checks, installed
preview delivery, European localization and full DAW parity remain open.
