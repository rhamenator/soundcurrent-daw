# ADR 075: staged native Windows capture boundary

Status: accepted for a bounded capture foundation, 2026-10-08. Windows desktop,
playback/monitoring, installation and full frozen parity remain required.

Use the Windows SDK's shared, event-driven WASAPI capture interface directly.
Do not transplant the equalizers' system-wide cable routing into the DAW. The
DAW opens an explicitly selected endpoint; discovery/default-role reads neither
change system defaults nor install a driver. Existing virtual endpoints can serve
as owned test routes. A cable is not a product setup prerequisite.

Keep COM, endpoint/format admission, events and buffer preparation on the native
owner thread before activation. The owner acquires and returns a whole SDK packet
once on that same thread. A sixteen-lease catch-up batch checks stop between leases.
No GUI/disk work enters prepared processing; capture uses the existing bounded raw
pipe/disk worker. Stop joins the native owner before callback context/buffer retirement.
The SDK/driver's internals and event waits are outside the prepared callback audit;
this design and short tests do not establish real-time scheduling deadlines.

`PreparedWasapiInput` has no Qt or Windows headers. It selects explicitly ordered
channels and deinterleaves into prepared planar banks. A packet can span at most
sixteen prepared DSP blocks. Admit the entire extent/flags/timestamp before capture;
SDK-declared silence ignores backing memory and becomes real zero samples. Do not
use amplitude to decide whether ordinary input is valid. Retain the initial native
flags/position/QPC. A first discontinuity has no prior packet; later discontinuities
and position gaps terminate capture and retain the first fault. QPC returned by
GetBuffer is already in 100 ns units; convert to ns without treating it as a raw
performance counter.

A native 44.1 kHz endpoint requested at 48 kHz produced 453 client frames followed
by position448, not position453. The preserved fault stops after the valid453-frame
prefix. Until a separate resampled/device timing contract is qualified, refuse a
project/endpoint rate mismatch during preparation. Do not conceal it by re-anchoring
or dropping samples. The fixture can choose the endpoint mix rate for its new test
project. A product UI must expose this restriction and later support qualified
rate conversion. This is a recorded implementation gap, not a reduced target.

Windows recording checkpoint inspection explicitly permits an existing writer to
coexist with a read-only partial-file inspector. Only this operation requests
FILE_SHARE_WRITE. Normal finalized media/hash/cache readers retain write exclusion;
new writers still cannot share the active writer. Native export tests verify an
attempted source mutation is denied and the valid export is unchanged, while Linux
continues to verify detection of an actual mutation.

No new third-party DSP/audio framework was adopted. Existing Windows SDK imports,
GCC/MinGW runtime, libsndfile and platform hashing remain the dependency boundaries.
This code is original GPL-3.0-only work; retained equalizer DSP snapshots are unchanged.
The read-only reuse audit found nine changed inputs in each equalizer repository;
review/adapt their localization/profile-editor changes before the corresponding
Windows GUI/reuse milestone.

Current native-channel admission is1..256 and one explicitly selected raw track in
the fixture. X006, all channels/layouts, multitrack owners, output rendering, device
notifications/reprepare, hardware alignment, failure/recovery, native Qt/localization,
ASIO/JACK choices and installer gates remain open. See the evidence checkpoint in
[96-windows-capture-foundation.md](../96-windows-capture-foundation.md).

Primary interface references (consulted2026-10-08):

- [GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
- [Buffer flags](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/ne-audioclient-_audclnt_bufferflags)
- [Loopback recording](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
- [Rendering](https://learn.microsoft.com/en-us/windows/win32/coreaudio/rendering-a-stream)

These references describe SDK contracts, not this application's acceptance evidence.
