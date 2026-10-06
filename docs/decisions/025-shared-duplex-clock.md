# ADR 025: one clock and cursor for file playback and armed raw capture

Date: 2026-10-06. Status: accepted for the M2d1 foundation.

Add a framework-free duplex bridge around the existing prepared mix run and raw
capture pipes. Validate one device block before either operation, copy raw input
before EQ/output writes, publish one common device origin and preserve per-track
accepted/rejected prefixes. Keep reader/writer lifetime and file/model work on
control/disk owners. Preserve terminal/fault facts independently of lossy meters.

Independent recording and playback filters would have separately admitted clocks
and risk alignment/drift. Reusing the equalizer's system filter-chain cannot
supply an in-process raw/project transport. The existing native PipeWire filter
infrastructure will host the new bridge; no server fork, new audio framework or
dependency is needed. A production native owner and desktop integration are the
next stage, with explicit rollback/join/take admission rather than fixture lifetime
assumptions.

Reuse the same per-track EQ/event/matrix graph for explicit Post-EQ live monitoring
by accepting optional prepared live replacements in the existing mix run. Off
retains file playback; Post-EQ replaces that track's file signal during this
generation. File offsets/underflow reporting still advance. Native input/output
aliases are safe because raw copies and all live staging precede output clearing.
Default offline rendering supplies no live replacements. Auto monitoring/punch
switching and full routing/PDC remain required. No monitor/profile EQ is printed
to raw assets. See [the contract and qualification limits](../35-shared-playback-capture.md).
