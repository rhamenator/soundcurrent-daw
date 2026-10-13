# ADR 150: extend comp transitions only through retained source handles

Status: implemented for raw or matching retained rendered assets. Separate
derivative rendering/admission and broader comp workflows remain open.

Use an explicit `PrepareCompTransition` transaction. Translate the existing
rational source map for backward extension, compare both sample endpoints with
the retained take window, preserve opposite fade phase, then apply the paired
fade law. Use existing exact wide-integer coordinate comparison. Avoid a second
position algorithm or implicit media creation. Charge temporary clips before copying.

The GUI requires an explicit handle option; ordinary crossfading continues to
require overlap. Metadata edits cannot make unavailable audio exist. Different
stretch derivatives need an asynchronous render/admission operation with matching
source/context/state proof before they can supply handles.

Acceptance: exact rational source-coordinate preservation, all-or-nothing group
and budget failures, recorded five-take independent PCM/live/export/reopen/raw
hashes, linked offsets, actual modal opt-in and Undo. Platform/installed/listening
and reference-mode gates remain separate. No full parity claim.
