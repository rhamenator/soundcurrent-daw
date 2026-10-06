# ADR-039: Separate a prepared raw capture range from playback

Date: 2026-10-06. Status: accepted engine foundation; full punch workflow open.

One continuous playback cursor and validated native clock remain authoritative.
An optional immutable half-open raw-frame range gates capture without stopping
playback. Prepare capture pools at its first frame; intersect each callback with
the range and use separate offset pointers. Explicit live monitoring sees the
full input block. Finish pipes at punch-out, retaining them until callback join.

Publish capture origin at the first recorded sample with checked integer offset
timing, not at preroll. Keep supplied alignment separate from diagnostic driver
delay. Preserve journal prefix semantics; completed capture does not imply every
written frame was committed if its disk owner was interrupted before finalization.

This primitive is not musical locator preparation, Auto monitoring, take selection
or looping. Add those through explicit prepared plans and versioned state; do not
mutate this range concurrently from the GUI or make discontinuous pushes into a
contiguous raw pipe. Native/Windows/full sustained performance and the failed
sanitized timeline admission workflow remain independent qualification gates.
