# Crossfade timeout diagnostics

The original Windows run 38806111667 timed out in desktop-paired-crossfade after
29.96 seconds, without flushed stage output. Checkpoint 346 adds flushed markers
before Qt startup/font resolution, timeline publication, each dialog opening and
callback, acceptance, layout checks and completion. Assertions and the timeout
are unchanged. Linux passes 163 checks; this is not proof of the native timeout
cause or repair. The original failed run remains fully retained in checkpoint345.

[Focused test log](../tests/results/M2/2026-10-15-crossfade-stages/tests.log).
Next obtain native stage evidence and continue timeline waveform integration.
