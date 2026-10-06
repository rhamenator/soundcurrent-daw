# Equalizer reuse update review

Owner instruction, 2026-10-06: keep DAW code/data borrowed from the two equalizer
projects updated as those projects develop in the other chat. This extends X005 and
all earlier reuse work. Existing source checkouts remain read-only here.

## Update procedure

Run `python3 tools/check_equalizer_reuse.py` before the next reuse-dependent milestone.
It verifies retained origin/review snapshot hashes and compares reviewed files with the
current sibling `soundcurrent-eq` and `soundcurrent-studio` working trees. Use
`--source-root PATH` for another checkout location. `--verify-snapshots` checks retained
inputs without either source checkout, suitable for a standalone source distribution.

Exit codes: 0 means these reviewed inputs match; 2 means one or more current inputs
need review; 1 means an audit/snapshot error. JSON names the affected files and hashes.
A changed HEAD alone is informational (`head_changed` in the JSON): unrelated commits
need not change an adopted component. If HEAD cannot be read, this field is null;
file hashes still determine whether registered inputs match. This is a read-only
check, not a background monitor or an automatic merger.
It covers registered reused inputs; newly introduced dependencies and source features
still need a human-readable impact review and expanded input inventory.

For each update, retain exact GPL source/data notices and hashes, review dependent
headers and changed behavior, adapt the DAW copy, and rerun affected acceptance tests.
Preserve DAW-specific validation, parameter identity, timing, channel/headroom policy,
project compatibility and monitoring-versus-printing behavior. Archive working-tree
sources with an observed HEAD **and** content hashes if the equalizer chat has not yet
committed them. Never label draft bytes as the content of that HEAD. Stable original
provenance stays intact; review history is additive. Keep unrelated equalizer work and
both repositories' branches/working changes untouched.

## Reviewed update: 2026-10-06

[Review inventory](../reuse/upstream-review.json) freezes the observed inputs, including
uncommitted widget/profile/DSP changes. Retained original snapshots remain in their
existing manifests; changed reviewed inputs live under `reuse/reviews/2026-10-06/`.
The root build has no dependency on either external checkout.

- **Equipment editor:** adopt the new bounded, half-Gaussian held-step acceleration
  in the frequency/gain/Q controls. Single taps remain ordinary steps; release,
  focus loss, window deactivation, hide, disable and wheel events reset the held timer.
  The DAW adapter retains its focused-only wheel handling and numeric ranges, and
  clamps scaled step multiplication to the integer domain before handing it to Qt.
  This arithmetic guard is a candidate for later equalizer adoption.
- **Catalog/schema/fitting:** inspected public inputs are identical to the already
  adopted equipment update, except for the widget substitution. No model corrections,
  schema metadata, measurements or library identities change in this update.
- **DSP:** the three reused Studio functions (`filterCoefficients`, `filterResponseDb`,
  `StereoEqualizer::Biquad::process`) are byte-identical to the original audited source.
  The new prepared-profile API moves coefficient/response preparation away from the
  processing step; the DAW already has a separate prepared engine/event model. Its
  stricter checks, stable IDs, planar channels, smoothing and floating-point headroom
  remain the applicable implementation. The equalizer's new enhancement chain and
  stereo endpoint deployment plans are outside the reused subset, so are recorded as
  reviewed context rather than imported as DAW effects or backend support.

Qualification is scoped to this update: [X005 evidence](../tests/results/X005/2026-10-06-equalizer-reuse-update.json) records two passing debug/sanitizer groups, the 2,233 equipment checks, 231,126 EQ checks and isolated audit failure cases. An isolated base-revision source tree plus the editor update was used because separate desktop export changes are still in progress. No full working-tree or export qualification is implied. It does
not establish Windows execution, delivered localization, measurement quality or full
X005 routing/portable-project support. These remain required product work. Current
sources can change again after this review; the next audit must detect that honestly.

## Committed-source refresh: 2026-10-06

The owner reported additional equalizer development. Rechecked public EQ
`080195a85be163cf8e4b1516745c63f43f317700` and premium EQ
`a63cb44ab4f166dc101cb5e93f8336d33d7e6b26`. All previously registered source bytes
remain identical to the retained review snapshots; the earlier draft DSP/editor/spin
changes are now committed. The original draft observation remains in
[the historical inventory](../reuse/reviews/2026-10-06/upstream-review-before-committed-refresh.json).
Per-file `source_revision` in the current inventory identifies the committed bytes.

The inventory now independently tracks all 12 inputs in **each** equalizer repository:
DSP source/header, profile editor source/header, accelerated control/test, collector,
catalog, catalog license/report/source registry and profile tests. The seven newly
registered premium inputs match the retained public GPL snapshots exactly. Independent
premium header, collector, catalog, license, report, source-registry and test edits
each trigger a review, even when the public copy remains unchanged.

Reviewed the changed-file inventory and new native Windows APO processor; recorded
ongoing guardian/virtual-driver work as context. These do not alter the borrowed EQ equations,
profile editor or data. They do not establish a DAW backend, Windows qualification or
new DAW effects. No compiled DAW source change is warranted by this refresh.

[Refresh evidence](../tests/results/X005/2026-10-06-equalizer-reuse-committed-refresh.json)
records the 24 matching inputs, retained snapshot integrity and the actual audit CLI's
isolated changed/missing/corrupt-input, informational-HEAD and source-preservation
checks. No C++ build or routing qualification is claimed for this metadata/tool update;
separate per-channel project routing work remains in progress. Continue this review
before each milestone that depends on reused components. Newly introduced features
still require an impact review, not just these registered-file hashes.
