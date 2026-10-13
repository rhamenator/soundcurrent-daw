# Recording disk-pause controls

Checkpoint 243 adds desktop controls for the prepared storage policy introduced
in [Checkpoint 242](242-recording-storage-budget.md). Full-suite completion and
Linux/Windows parity remain active goals.

## User workflow

In native recording setup, enable **Buffer for disk pauses**, enter the desired
pause duration and explicitly choose the smallest input/output blocks allowed by
the audio setup. No minimum is inferred from the maximum packet size. Unchosen,
inconsistent or over-budget settings disable Prepare and show the reason.
The duration range is 1–60,000 ms; capacity and memory admission can refuse a
value within that range. The initial offered duration is 3,000 ms, with minima
unselected until supplied by the caller or user. Legacy buffering remains selectable
and makes no duration promise.

The preview shows duration, actual slot counts and the complete planned route
payload. It is recalculated only when settings/context change, not on the 25 ms
status timer. It does not activate streams, create recording files, allocate PCM
or receipt banks, or reserve resource-ledger credit. It copies bounded declarations
and uses existing validation objects; it is not an allocation-free callback API.

After Prepare, an immutable worker snapshot supplies effective capacity and the
actual route payload charge. Storage settings are locked until the route/result is
released. Record uses the already prepared route. Actual smaller blocks retain
the backend's explicit InvalidQuantum failure. A changed graph requires a new
preparation. Finite buffer duration is not a guarantee against sustained slow disks,
repeated pauses, scheduling failure or unrelated bounded-queue pressure.

Saved take inspection/reopening shows the saved declaration and explicitly states
that no audio route is allocated. It does not present an old charge as current
memory use. Reset clears the worker capacity snapshot without deleting the take.
Amounts are locale-formatted; labels have accessible names and input buddies.
The recording section remains inside the main window's scrollable content.

## Platform and localization boundaries

The policy is implemented for the owned PipeWire route, including its portable
Windows test seam. The native WASAPI route still needs independently prepared
storage admission, actual variable capture-packet/return geometry, native minimum
checks and saved policy state. The shared panel clearly disables this option on
that route. A Windows build or passing portable UI test does not establish native
Windows budgeting parity. This gap must be implemented rather than treated as a
completed feature.

Real Qt Linguist extraction updates all 34 existing catalogs and the source hash
inventory. New text remains unfinished outside English unless translated; zero
native-language review or full European UI qualification is claimed.

## Acceptance and next work

Acceptance workflows include: absent minima refuse preparation; an excessive
stall or mismatched output minimum stays refused across timer polls; a valid
three-second/480-frame policy previews 302 raw/receipt slots; estimate consumes
no ledger credit and matches the actual prepared payload; controls lock during
prepared recording; finite capture/EQ, inspection, export, timeline admission and
reopening retain original behavior; reopening labels saved capacity accurately.
Source `bf932b7c51f19f200a4131cf8f0cbf581249e95b` is bound in
[the evidence index](../tests/results/M2/2026-10-13-recording-storage-panel/index.json):
77 compressed text/configuration/metadata members and three local executable hashes.
Fresh single-worker Release build: 189 steps, then the final incremental inputs.
Disk-stall gate passes 2.38 s; desktop localization passes 0.70 s. The corrected
visible native panel gate passes **13.28 s** (13.29 s CTest total).
The first panel gate timed out at the mouse-driven Record action: preparation
succeeded, its tab remained hidden, and its screenshot was a collapsed title bar.
Selecting the actual recording tab fixes the test; no production timeout was
relaxed. The original zero-frame cancelled state and failed log are retained.

The prepared panel image was inspected at 762×455: controls and capacity text are
readable without clipping. The planned payload equals the actual admitted payload,
and preview ledger usage remains zero. GUI workflows use simulated stream owners,
not physical devices. Localization extraction and structural audit pass for 34
catalogs/1,002 source keys; native review/full UI language qualification stay zero.

Original PR176 run38338037928, sourcecdbdc8a, is fully terminal and retained:
Windows native CTest **84/84 in262.93 s**, followed by a failed separate concurrent
owner diagnostic; Windows desktop **19/20 in85.48 s**, failure in its native
Windows device-selection recording with raw QueueFull; Linux **155/157 in428.54 s**,
failures in desktop native panel and manual recording control; cross-build passes.
The failed Windows desktop trace reports native raw126240, capture-delivered126720,
consumed29760 and raw fanout cause14. It does not show a PipeWire failure: that
workflow passed. The old failures remain separate from the new local success.
Original archives/failed binary media stay local; public evidence is text metadata.

Next: qualify this panel on Windows independently, retain original CI failures,
and implement native WASAPI budget admission from actual prepared descriptors
without guessing packet minima. Continue interrupted-original verified-copy review
and remaining recording work, then the full staged professional-suite backlog.
