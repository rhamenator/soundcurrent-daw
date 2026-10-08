# ADR077: native route identity and storage completion

Status: accepted for the Windows preview boundary, 2026-10-08.

Store stable backend/device/channel identifiers independently of translated or
friendly text. Choose the project rate explicitly; native preparation revalidates
current endpoints and channel maps before activation. Initial Windows streams
require project/device mix rates to agree. Do not force device settings, select
defaults, downmix or combine clocks. Retain the shared framework-independent engine
and existing PipeWire implementation.

Processing and storage completion are separate domains. Show a disk error after
the final callback independently of the retained processing terminal status. Join
the owner, retain other valid takes and preserve the failed writer's checkpoint
and original error. Require the deterministic late-error regression workflow;
extending the polling timeout does not fix it.

Use the available MSVC Qt 6.12 SDK for native developer GUI qualification. Build
libsndfile with the same MSVC/UCRT: its descriptor API must share the caller's CRT
descriptor table. Do not mix the earlier MinGW DLL with MSVC media callers. Use
SDK interface UUID attributes for both compiler families, following
[Microsoft's MMDevice example](https://learn.microsoft.com/en-us/windows/win32/coreaudio/mmdevice-api).

Native conversion, independent-device drift, Windows duplex/monitoring and clean
installation remain in scope. [Implementation and evidence](../98-windows-desktop-foundation.md).
