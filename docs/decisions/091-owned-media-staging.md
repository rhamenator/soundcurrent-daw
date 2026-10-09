# ADR091: verified owned media staging before project conversion

Status: selected; Linux and protected native Windows byte-staging qualification
complete for PR69; publication/recovery/installed/full-parity gates tracked separately.
Date: 2026-10-09.

| Alternative | Functionality, license, maintenance and cost | Decision |
|---|---|---|
| filesystem::copy_file using selected path strings | Standard C++; reopens mutable source/destination names and does not bind the checked source or hash readback | Refused for this boundary |
| Existing ProjectStore publish helper | Original GPL; replaces existing owned JSON, has neither exclusive foreign-media staging nor selected directory authority | Keep its current purpose |
| New copy/crypto framework | Additional maintenance, licensing, platform/dependency burden | No demonstrated need |
| Original GPL handle-relative staging with existing OpenSSL/BCrypt | Reuses qualified source capability and provider, requires platform authority/durability qualification | Selected |

The unchanged original SHA-256 helper is moved into a private shared header; source
and destination hashing use existing provider code and grants. No vendor code,
new runtime library, proprietary algorithm, driver or equalizer snapshot is added.
All I/O, hashing, handle creation/retirement and allocation remain outside audio.

This increment provides verified partial staging, not a complete import transaction.
No completion marker, original occurrence receipt, automatic cleanup, recovery or
Session adoption is implemented. Do not install/expose the development worker as a
finished copy workflow. Add immutable versioned provenance and exclusive commit/
recovery before GUI project conversion. Windows file-only durability and Linux
trusted namespace/mkdir-open limitations remain explicit. See checkpoint125.
