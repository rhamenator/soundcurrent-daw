# ADR 061: manual recording preview and desktop lifetime

Status: accepted,2026-10-07.

Keep GUI intent/previews in a dedicated Qt panel, canonical project/history on the
project controller and native/reader/disk ownership on the serialized manual worker.
The framework-independent engine and std-thread controller remain independent of Qt.
Use the canonical command barrier rather than the selected-track inspector projection.

Worker completions and audio punch/parameter receipts are separate. Disable dependent
GUI gestures immediately after admission and retain priority Stop/Cancel during IO
and finalization. Coalesce parameter models while preserving accepted event prefixes;
only per-track audio acknowledgements establish whole-model application.

Adoption explicitly verifies a group through ProjectController. Retain the preview
until its expected asset receipt arrives; failure enables retry. Canceled receipts
remain recovery-only. Close drains first and requires a preview decision before
the dirty-project prompt. The worker is not shut down on initial Close because
the user may choose Review/Cancel and continue editing or recording.

Correlate attachment completions/rejections by caller-owned IDs rather than the
project's global error serial. Unrelated rejected commands may occur while IO is
held or queued. Keep terminal IO and control-rejection receipts separate, scoped
to the current single serialized desktop attachment owner; do not claim arbitrary
multi-consumer reliability from two retained receipts.

Place fixed-range/locator and manual/repeated-take workflows in separate tabs.
Both retain their current finite preparation bounds. This does not establish full
recording parity, native Windows or above256-track scalability; see docs/76.
