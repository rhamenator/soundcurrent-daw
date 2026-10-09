# Third-party notices

## Shipped source

**SoundCurrent Studio GPL DSP subset**, pinned at `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b`. Original SPDX GPL-3.0-only notices and unmodified source snapshots are retained in [reuse/studio](reuse/studio/README.md), with hashes and documented modifications. Adapted peaking mathematics/recurrence compile in the new DAW engine. The repository GPL license is included; no separate file-level copyright declaration existed in those sources.

**JSON for Modern C++ 3.12.0**, copyright Niels Lohmann, MIT license. The original copyright and permission text is retained in [LICENSE.MIT](third_party/nlohmann/LICENSE.MIT) and the vendored header. Exact source URLs and SHA-256 hashes are in [the manifest](third_party/manifest.json). Used only on control/worker threads; no JSON code enters a real-time callback.

**libsndfile 1.2.2 public API header**, copyright1999–2016 Erik de Castro Lopo, LGPL-2.1-or-later. Unmodified header/notices and COPYING are in `third_party/libsndfile`, with exact source hashes in the manifest. Used by root disk-side recording/read-ahead and the isolated feasibility probe, outside the real-time engine. Linux dynamically links distribution libsndfile1.2.2-4. Windows development cross-build dynamically links upstream1.2.2 with external/MPEG codecs disabled; archive/build pin is in the manifest, full source stays in ignored cache. Actual library/codec transitive packaging notices must be inventoried before distribution.

**SoundCurrent EQ equipment-profile adapter**, pinned at the exact `6b53056` revision
in [reuse/equipment/provenance.json](reuse/equipment/provenance.json). Original
GPL-3.0-only SPDX notices and unmodified source/header/curve reference snapshots remain
under `reuse/equipment/upstream`; adapted Qt editor and response-only C++ helpers are
marked modified 2026-10-05. These helpers are not audio processing paths.

**Spinorama generated EQ catalog**, source revision
`acc757bb98d63327092ee537bde25d9c227811f3`, adopted through that equalizer snapshot.
Its upstream GPLv3 license, bounded collector, count/gap report and per-entry source
URLs/hashes are retained under `reuse/equipment/upstream`. The root README identifies
GPLv3; original plots, third-party articles/calibration arrays and the Pyle electrical
transcription are not included. The local 1,087-entry catalog is an adapted generated
correction set. Complete per-source data-rights and corresponding-source qualification
remain a release gate. Install rules include catalog license and reuse provenance;
these rules do not themselves qualify a distribution package.

**libsamplerate0.2.2 compiled kernel/public API**, Erik de Castro Lopo and
contributors, BSD-2-Clause. The
complete unmodified source and copyright/license are retained in
[third_party/libsamplerate](third_party/libsamplerate/COPYING). The manifest pins
the official archive and every source file. The original DAW prepared component
uses static best-sinc conversion outside callbacks; it is not yet linked into
Session playback/export or the installed desktop. No new external codec/FFT
library is linked. Native Windows, full processing quality and final binary
notices remain separate gates. Fast/medium converters, upstream examples/tests
and install rules are disabled in a scoped CMake adaptation; upstream source is
unchanged. System C runtime/math dependencies require platform inventory.

The complete source also retains separately licensed, unused helpers. Six
Autoconf macros (`ax_append_compile_flags`, `ax_append_flag`,
`ax_append_link_flags`, `ax_check_compile_flag`, `ax_check_link_flag`,
`ax_compiler_vendor`) are GPL-3.0-or-later; `ax_recursive_eval` is
GPL-2.0-or-later, distributed here under its later GPL-3.0 option. Each preserves
its original copyright and special exception for Autoconf-generated configure
scripts; that exception does not relicense the macro itself. The root LICENSE
contains GPL-3.0. `ax_compiler_version` (Bastien Roucaries) and
`ax_require_defined` (Mike Frysinger) preserve their
[FSFAP notices](https://spdx.org/licenses/FSFAP.html). `clip_mode.m4` retains
Erik de Castro Lopo's file-specific permission to use/copy/modify/distribute/sell
with its notice and disclaimer. `cmake/FindFFTW3.cmake` retains Wenzel Jakob's
2015 BSD-2-Clause notice. None of these helpers is used by the selected CMake
kernel build. All original bytes, notices and exceptions remain hash-pinned;
`additional_source_notices` records their exact paths. Files without a specific
notice use upstream's COPYING; final distribution auditing remains a release gate.

## Linked system dependencies of the current core

- Linux: OpenSSL Crypto, version **3.5.5** in this development build. Apache-2.0; [upstream license](https://github.com/openssl/openssl/blob/openssl-3.5.5/LICENSE.txt). It supplies worker-side media SHA-256. Source and binaries are not vendored. Distribution packaging must retain the actual linked version's notices/license and audit transitives.
- Optional Linux native adapter: system **PipeWire1.6.2**, MIT upstream, with exact API/lifecycle source review hashes in [the manifest](research/pipewire-1.6.2-review.json). No PipeWire source, server, or runtime is bundled. Actual library/SPA/transitive runtime notices and corresponding distribution inventory remain package gates.
- Windows: operating-system BCrypt and Win32 file APIs. No additional crypto DLL distributed. The current MinGW cross-build also uses its C++ runtime; runtime deployment/license notices must be audited before packaging.
- C++ standard/compiler runtimes: system components for development; exact binary runtime dependencies require platform package inventory before release.

The optional desktop target system-links **Qt6.10.2 Core/Gui/Widgets** in this development build; **Qt Test** is used only by its UI fixture. Module sources are not vendored; distribution module/transitive notices, exact open-license selection and corresponding source obligations remain package gates. Installed Qt-base copyright/license metadata is inventoried with the S6c evidence. The reusable core does not link Qt. JACK and plugin SDKs are not linked by the current root build. PipeWire is linked only by the optional Linux adapter/integration fixture, outside the framework-free core. Standard/Win32 thread APIs now also run the production disk supervisor. libsndfile transitive codecs linked by the Linux distribution library (FLAC/Vorbis/Opus/Ogg/mpg123/LAME, and compression/runtime libraries) require their exact package notices before distribution; the Windows minimal DLL imports only KERNEL32/msvcrt in this build, while embedded GSM/ALAC and compiler components retain separate source notices and still require complete package inventory. The isolated `experiments/` build has separate dependencies described in the [inventory](docs/03-dependencies.md). Local preview package preparation and installed workflow evidence are tracked separately in docs/89-workflow-previews.md.

## Additive equalizer source review (2026-10-06)

The GPL-3.0-only held-step numeric widget in `ui/accelerating_spinbox.hpp` is
adapted from the exact equalizer working snapshot retained under
`reuse/reviews/2026-10-06/`. Original SPDX notices remain.
`reuse/upstream-review.json` records observed source HEADs, committed/draft state,
SHA-256 hashes and adaptation details. Reviewed DSP/editor sources are archived
references, not compiled wholesale; existing exact origin notices remain applicable.
No new third-party library or measurement redistribution is introduced.

## Additive localization review (2026-10-08)

GPL-3.0-only equalizer localization/numeric-input/catalog updates are retained
under `reuse/reviews/2026-10-08-localization`, pinned to public EQ
`6081fd4a25d19b8fd15121e67c5852f9af1f1ac5` and Studio
`a6d152b2b29530123fe517f02d2cfbc3db8cdd3f`. Original SPDX notices remain.
Explicit context/source draft-word mappings and adapted runtime/tool behavior
are described in [checkpoint 100](docs/100-localization-reuse-refresh.md).
These draft words carry no independent native-language certification. Original
DSP/equipment provenance remains unchanged; no proprietary assets are copied.

## Windows MSVC preview deployment (2026-10-08)

The bounded Windows installer preparation uses **QtBase 6.12.0** shared
Core/Gui/Widgets plus Windows platform/style and GIF/ICO/JPEG plugins from the
open-source MSVC x64 distribution. Qt Test is test-only. QtBase license texts and
its supplied SPDX SBOM accompany the payload; the pinned complete QtBase source
archive accompanies setup. Shared libraries remain replaceable. Qt's bundled
third-party license texts are retained; final content/data-rights and complete
platform release auditing remain separate gates.

**libsndfile 1.2.2**, LGPL-2.1-or-later, is built as a shared x64 MSVC/UCRT DLL
with external/MPEG codecs, programs, examples, tests and experimental features
disabled. Its LGPL text, embedded ALAC/GSM notices and exact unmodified complete
source archive accompany this preview. This describes the native MSVC preview,
not the older MinGW DLL or the Linux distribution's codec set.

The official **Microsoft Visual C++ x64 Redistributable 14.44.35211.0** comes
unchanged from the licensed Build Tools installation. Its Microsoft signature
was Valid. The independent Microsoft installer controls its license, installation
and elevation; no developer CRT DLLs or SDK are copied into the app payload.
Windows system libraries remain OS components.

The generated Unicode installer uses **NSIS 3.10-2**. Its distribution copyright
and license notices accompany setup. SoundCurrent's installer wrapper is unsigned.
Source identity, SHA-256 pins and qualification boundaries are documented in
[checkpoint 101](docs/101-windows-installer-preparation.md).
