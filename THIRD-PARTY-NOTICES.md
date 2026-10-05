# Third-party notices

## Shipped source

**SoundCurrent Studio GPL DSP subset**, pinned at `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b`. Original SPDX GPL-3.0-only notices and unmodified source snapshots are retained in [reuse/studio](reuse/studio/README.md), with hashes and documented modifications. Adapted peaking mathematics/recurrence compile in the new DAW engine. The repository GPL license is included; no separate file-level copyright declaration existed in those sources.

**JSON for Modern C++ 3.12.0**, copyright Niels Lohmann, MIT license. The original copyright and permission text is retained in [LICENSE.MIT](third_party/nlohmann/LICENSE.MIT) and the vendored header. Exact source URLs and SHA-256 hashes are in [the manifest](third_party/manifest.json). Used only on control/worker threads; no JSON code enters a real-time callback.

**libsndfile 1.2.2 public API header**, copyright1999–2016 Erik de Castro Lopo, LGPL-2.1-or-later. Unmodified header/notices and COPYING are in `third_party/libsndfile`, with exact source hashes in the manifest. Currently used by the isolated Linux media feasibility probe, not the root engine. The probe dynamically links the installed libsndfile1.2.2 runtime; library code is not vendored. Actual library/codec transitive packaging notices must be inventoried before distribution.

## Linked system dependencies of the current core

- Linux: OpenSSL Crypto, version **3.5.5** in this development build. Apache-2.0; [upstream license](https://github.com/openssl/openssl/blob/openssl-3.5.5/LICENSE.txt). It supplies worker-side media SHA-256. Source and binaries are not vendored. Distribution packaging must retain the actual linked version's notices/license and audit transitives.
- Windows: operating-system BCrypt and Win32 file APIs. No additional crypto DLL distributed. The current MinGW cross-build also uses its C++ runtime; runtime deployment/license notices must be audited before packaging.
- C++ standard/compiler runtimes: system components for development; exact binary runtime dependencies require platform package inventory before release.

Qt, PipeWire, JACK, plugin SDKs and other planned libraries are not linked by the current root build. Test-thread runtimes/Win32 test thread APIs are used only by the headless test harness. The isolated `experiments/` build has separate dependencies described in the [inventory](docs/03-dependencies.md). There is no installable DAW package yet.
