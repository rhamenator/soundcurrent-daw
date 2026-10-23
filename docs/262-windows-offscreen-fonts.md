# Windows offscreen fonts and original recording wait evidence

Checkpoint 262 retains the original terminal Windows failures and corrects the
font environment used by offscreen desktop tests. It does not install a preview
or qualify physical endpoints, installers, languages or full-suite parity.

## Original failures

Hosted run 38480424472 tested source c005535c38347c444972827ebd0643110554e974.
Linux and the Windows core cross-build passed. The Windows desktop job failed its
350×240 crossfade horizontal-overflow check (viewport 312 / body 342 / horizontal scroll range 30).
Both retained dialog captures rendered English text as missing-glyph boxes.

The exact retained QtBase 6.12.0 source shows that Windows offscreen integration
uses QFreeTypeFontDatabase. That database obtains its directory through
QPlatformFontDatabase::fontDir(): QT_QPA_FONTDIR, or the Qt library fonts directory.
The original captures show no usable English font was resolved. CI now points the offscreen test
processes at the host's existing %WINDIR%\Fonts. A QRawFont guard refuses missing
application fonts or absent printable English glyphs before dialog evidence can
qualify. Existing wrapping, overflow and screenshot checks remain in place.
Native application font selection and dependency pins are unchanged.

The same original run failed native-monitor-startup: the 44.1 kHz mono-to-stereo
owned finite recording did not complete within its original five-second wait.
At expiry it had accepted/copied 88,641 frames, published no monitor output, and
the simulated SDK was waiting for disk acknowledgement of packet 201 after 200
acknowledgements. A previous service cycle took 2.400 seconds. This is insufficient
to attribute the delay to a specific disk operation or descheduling.

Test-only disk/control observations now record the current original-recording
boundary and maximum elapsed time between boundaries. The timeout snapshot is
captured before cancellation, then retained with the joined durable metadata.
Atomic fields are independent, not a claimed coherent device timestamp; elapsed
time includes descheduling and is not a measurement of storage device latency.
No callback work, five-second wait, durable checkpoint interval or success
criterion is changed. A passing later run does not explain the original delay.

## Engine continuation

The ignored missing-handle feasibility planner now covers paired rendered and
mixed raw/rendered fragments, stale project/rate/clip/source/take state, fractional
coordinates, contexts, warp extrapolation, 44.1/48/96 kHz timing, nested retained
processed takes, and releasing the first reservation when the second plan fails.
It remains metadata-only: no missing audio, worker publication, project admission
or sonic-quality qualification. Before promotion, reuse shared exact interval
arithmetic; seal plans against mutation and bind project epoch/root. Actual
worker rendering, versioned atomic admission, Undo/reopen/cancellation and
SRC-neighborhood/warp/context edge quality remain required.

The current and previous installed Ubuntu previews and the equalizer repositories
remain unchanged. All full-suite and all-Europe completion criteria remain open.

## Validation

[Source-bound evidence](../tests/results/M2/2026-10-13-windows-offscreen-fonts/index.json)
records implementation `e5ab0be7c7a80842d7ad3f46d27f6401bc01cc9d`, input and
executable hashes, actual process IDs, and original Windows logs/captures. A fresh
single-worker Release build on Linux passes 165 real dialog/font checks,
5,932,791 composed native-monitor startup/recording checks with simulated SDK
streams and production processing/disk workers, plus both catalog checks. Callback
allocation/free/blocking-lock audit counts remain zero. The metadata-only planner
is recompiled and passes 136 checks; it is not a production rendering implementation.

Hosted [run 38483626019](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38483626019)
is still in progress at this checkpoint. Preserve its original terminal result
before further branch updates; Linux success does not qualify Windows. The
original recording delay remains unexplained even if this later run passes.
