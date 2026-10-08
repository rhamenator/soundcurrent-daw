# Device-format diagnostics

This checkpoint advances the recording/playback preview and X007 installation
experience. The frozen Bitwig/Cubase target and F/Q/C/N gaps remain unchanged.

## Workflow

After preparation, select every required input, output and monitoring channel.
The UI explains incompatible WASAPI selections before enabling Play or Record:

- A rate mismatch shows the device name, device rate and project rate. Choose
  a matching device, or change the device rate in the system audio settings
  and prepare again. Creating a project at a supported device rate is another
  option; this checkpoint does not retime an existing project.
- Selecting channels from different devices for one current WASAPI stream
  asks for channels from one device.
- Reusing a native channel asks for distinct channel selections.
- Missing or invalid selections keep Start disabled. Stop remains available
  for a prepared or active stream.

An incompatible saved route is still an authored choice. Save/reopen retains
it and shows the reason; the app does not select a fallback or change project
rates. Compatibility is checked again when the worker consults fresh inventory
before stream activation. GUI preflight cannot establish continued device
presence across a hot-plug race.

## Implementation and decision

`checkWasapiPorts` is a Qt/SDK-independent typed control-side check shared with
`selectWasapiPorts`. It reports the offending channel without allocating or
activating streams. Its bounded channel/layout/rate checks describe the current
native stream adapter. The authoritative selection still requires exact fresh
inventory membership before native preparation.

The GUI translates issue messages in its own context and formats rates with
the chosen locale and directionally isolated units. All 34 catalogs have been
regenerated from 568 source keys. The 33 non-English catalogs remain partial
unreviewed drafts; this checkpoint does not promote language coverage.

The GUI gathers selected ports on the control thread. Processing callbacks,
raw capture, engine parameters, project serialization and default system routes
are unchanged. PipeWire's existing rate negotiation remains available; WASAPI's
current mix-rate restriction is not imposed on PipeWire selections.

No new library or license is introduced. The existing framework-independent
port validation library is linked into the GUI's playback dependency surface
on both platforms. Keeping one typed validator avoids separate UI/backend
rules while preserving activation-time freshness checks.

## Acceptance and boundaries

The [retained receipt](../tests/results/X007/2026-10-08-device-format-diagnostics.json)
records Linux focused/full UI and sanitizer results, native Windows interactive
Qt results, source-input identities and the initial Linux test timeout.

The tests exercise rate mismatch, missing selections, multiple output devices,
duplicate channels, microphone and monitoring-output mismatch, saved mismatch
reopen, and explicit correction followed by successful playback/recording.
They assert that incompatible choices create no active endpoint or recording
job, that Stop stays usable, and that project rates/routes remain authored.
The existing full UI suite verifies the surrounding EQ/Undo/save/recovery and
worker-retirement workflows. An older playback test now waits for the newly
conditional Play button before clicking; its original timeout is retained.
The first hosted full run found the same readiness assumptions in export and
mix tests. Both are corrected to choose outputs and observe readiness; mix also
asserts that Play is disabled before selection. Linux, sanitizer and native
Windows export/mix tests pass, with the hosted failures and original capsule
payloads retained. These corrections do not change production code.

Native Windows Qt tests use owned fake audio endpoints. They qualify these
controls separately from SDK/audio activation, physical devices and sustained
scheduling. Windows shared-clock monitoring/duplex, automatic rate conversion,
independent-device drift handling and non-silent startup remain open.

The existing installable local previews use their earlier frozen sources. New
diagnostics require a separately source-paired, qualified preview build before
being represented as delivered in an installer.

Next implementation: address native startup/timing behavior with an owned
non-silent source and retained first-buffer observations, then qualify physical
recording/playback without silently accepting discontinuities. Rate conversion
and independent-clock adaptation remain in the full backend backlog.
