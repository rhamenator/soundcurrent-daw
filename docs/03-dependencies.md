# Dependency, license and maintenance inventory

Assessment date: 2026-10-05. **Evaluated** does not mean **installed**, **vendored**, or **approved for every use**. The root build now uses nlohmann3.12.0, platform SHA-256, the audited GPL peaking subset from Studio, disk-side libsndfile and an optional Linux PipeWire adapter. The old isolated DSP/WAV probe remains separate. No system packages were installed.

Repository head/commit dates and archive flags are captured in [dependency-observations.json](../research/dependency-observations.json). These are investigation snapshots, not release pins. Before adding a dependency choose an actual supported release/commit, record SHA-256 and SPDX notices for all transitive code, inspect license exceptions and run its intended-use integration gate. GitHub's detected license is a hint; text in the exact source governs.

Recent commits are a maintenance signal, not a guarantee of support, review quality or a stable release. Libraries not compiled here retain an integration gate; no untested candidate is presented as a finished component choice.

## Recommended boundaries

| Candidate and primary source | Functionality / Linux fit | License assessment | Maintenance evidence / cost | Decision and required gate |
|---|---|---|---|---|
| [Qt6](https://doc.qt.io/qt-6/licensing.html) | Desktop GUI, accessibility, model/view; Linux Wayland/X11 | Core/Gui/Widgets available under LGPL3/GPL3/commercial terms; modules/transitives vary | Local6.10.2; major active toolkit; moderate UI integration cost | Selected Qt6 Core/Gui/Widgets for the opt-in S6c desktop adapter; fixture-only Qt Test. System6.10.2 Linux controller/window/tests run, with6.4 as an unqualified API floor. Default build stays Qt-free. No Qt in engine; actual module/transitive notices and release packaging still require audit. Reassess Quick for large editors after profiling. |
| [PipeWire](https://docs.pipewire.org/group__pw__filter.html) | Native graph-scheduled multitrack Linux I/O and ports | MIT upstream; bundled notices still need inventory | Localdev1.6.2; inspected webAPI1.6.9, verify local headers; moderate adapter cost | S5 links system libpipewire, current API minimum1.6.2, with source/lifecycle pin in `research/pipewire-1.6.2-review.json`. Owned source→track→sink and source-removal tests pass; explicit routes preserve defaults/existing links. Hardware latency, deadline, mlock, reprepare and normal module-unload LeakSanitizer qualification remain gates (context-only dependency reproduction documented in S5). Older distribution APIs need qualified adaptation before packaging; no global EQ route transplant. |
| [JACK2](https://github.com/jackaudio/jack2) | Mature callback/transport/MIDI integration; PipeWire compatibility | Library and server licenses differ; verify chosen libjack LGPL terms and server GPL separately | Mature Linux infrastructure; missing local dev/tooling; moderate adapter cost | Later backend; consume system libjack. No fork/server bundled for first slice. |
| SoundCurrent Studio SDK at `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b` | C++20 EQ/delay/reverb/router and WAV; headless Linux/Windows | GPL-3.0-only; preserved notices/snapshots/hash provenance in `reuse/studio` | Owner-maintained; tested subset now adapted here | Selected peaking math/recurrence imported into prepared DAW EQ; original repo unchanged. No global routing, forced clipping, full-state configure or bridge threading adopted. Full graph/host functionality remains new work. |
| [libsndfile](https://github.com/libsndfile/libsndfile), [1.2.2 release](https://github.com/libsndfile/libsndfile/releases/tag/1.2.2) | Worker-thread WAV/RF64/BWF and other sampled formats | API header LGPL-2.1-or-later; original notices/COPYING retained, source hash pinned; optional codecs/runtime/transitives still need package inventory | Installed runtime1.2.2; pinned header added without system packages; release2023-08-13; Linux package1.2.2-4 inventoried; Windows codec-disabled source archive pinned | Linux S4 worker/float RF64/checkpoints/recovery/SIGKILL/kernel-short-write/independent sample-reader fixtures pass. Physical power-loss/ENOSPC and >4 GiB remain pending. S6a also uses it for validated owned WAV/RF64 clip read-ahead and file-backed playback; native Linux mono/owned-sink and headless mapping/seek/failure fixtures pass. Root disk-side media target now links it; RT transport/engine remain independent. Windows minimal upstream1.2.2 DLL, disk worker/tools/tests cross-link; native execution and full packaging/license-transitive qualification remain required. |
| [nlohmann/json 3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0) | Versioned human-readable project snapshots on control/worker threads | MIT; header and license vendored with exact SHA-256 in `third_party/manifest.json` | Released2025-04-11; header-only; moderate parser-memory overhead | Selected and implemented for v1.0 project snapshots off RT. Depth32/4 MiB and object/count limits, duplicate-key and strict numeric/version fixtures. Migrations still pending. |
| [OpenSSL 3.5.5](https://github.com/openssl/openssl/blob/openssl-3.5.5/LICENSE.txt) / [Windows BCrypt](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash) | Worker-side streaming SHA-256 for media; Linux system Crypto / native Windows API | OpenSSL Apache-2.0; Windows OS component, no crypto DLL shipped | Local Linux3.5.5; platform-specific small adapter | Implemented. Known SHA fixture passes Linux; BCrypt path cross-compiles but native runtime qualification pending. Keep all hashing off RT. |
| [SQLite](https://www.sqlite.org/copyright.html) | Content/media index, searchable browser and recovery catalog | Public domain upstream; shipped wrapper/source provenance recorded | Long-established; transactions suit workers; low integration | Provisional index after M1; project state remains explicit JSON/media. No audio-thread DB operations. |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk), [license explanation](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST%2B3%2BLicensing/Which%2Bfiles%2Bfall%2Bunder%2Bwhich%2Blicense.html) | Native Linux instrument/effect ABI, expressions, buses and editors | Current inspected SDK is **MIT** (3.8 era); do not assume older SDK license. Logos/trademarks separate | Snapshot2026-08-11; upstream host examples; high host/IPC/editor integration cost | Select direct adapter at M6, pin SDK + submodules, build conformance fixtures. No automatic VST2 rights or proprietary plugin redistribution. |
| [CLAP SDK](https://github.com/free-audio/clap), [reference host](https://github.com/free-audio/clap-host) | Linux hosting, event/modulation/voice APIs, explicit thread/latency/tail extensions | MIT SDK/reference host; individual plugin license separate | SnapshotSDK2026-07-28; reference host2026-06-18; high lifecycle/editor cost | Select direct adapter at M6. Stable extensions first; drafts capability/version gated. Reference host is not the DAW engine. |
| [LV2](https://lv2plug.in/), [Lilv](https://drobilla.net/software/lilv) | Native Linux plugin metadata/state/worker/atom/UI ecosystem | Primarily ISC-style code licenses; each bundle, vocabulary/document and UI dependency separately inventoried | Established specialized tools; read GitLab release history before actual pin; moderate-high adapter cost | Select Lilv discovery/hosting at M6 subject to pinned source/transitive Serd/Sord/Sratom audit. No RDF/state disk operations in RT. |
| [libsamplerate](https://github.com/libsndfile/libsamplerate) | Streaming/sample-rate conversion, Linux C interface | Current inspected code BSD-2-Clause, older licenses must not be assumed | Snapshot2026-08-13; mature0.2.2 README; low integration, quality/latency verification needed | Provisional after Q-RESAMPLE; do not claim it solves independent-device drift without control logic. |
| [Rubber Band](https://github.com/breakfastquay/rubberband) | Time/pitch APIs, R2/R3 modes and live shifter | GPL2-or-later code or commercial terms; verify selected build/FFT dependency combination | Inspected mirror2025-02-27; DSP-specific API; high algorithm-tuning cost | Provisional Q-STRETCH/Q-PITCH bakeoff. GPL path compatible in principle with GPL3 project after exact source review; no quality equivalence assumed. |
| [KissFFT](https://github.com/mborgerding/kissfft) | Prepared small FFT analysis off RT; compact Linux implementation | BSD-3-Clause code; inspect COPYING because autodetection says NOASSERTION | Snapshot2026-08-12; small dependency; low integration cost | Prefer over maintaining handwritten UI FFT after profile/normalization tests. Worker analysis only initially. |
| [FFTW](https://www.fftw.org/) | Optimized large transforms and planning | GPL2-or-later/commercial options; confirm exact config | Established; plans can allocate/block, SIMD/platform tuning adds cost | Alternative if measured analysis/convolution workload justifies; never plan in RT. Not selected for M1. |
| [libebur128](https://github.com/jiixyj/libebur128) | Loudness and true-peak calculations | MIT | Last inspected commit2021-02-14: stale maintenance risk despite usefulness; low integration, audit/replacement budget | Conditional; published loudness corpus and RT allocation audit mandatory. Keep on analysis worker. |
| [FluidSynth](https://github.com/FluidSynth/fluidsynth) | SoundFont instrument engine, native Linux support | LGPL2.1 family; soundfonts separate | Snapshot2026-10-04; active platform CI; moderate note/RT/voice integration | Candidate for original sampler/instrument workflows, not full instrument catalog equivalence. Disable internal device/server/thread ownership when embedding. |
| [sfizz](https://github.com/sfztools/sfizz) | SFZ sampling engine | BSD-2-Clause core; dependencies and sample licenses separate | **Inspected repository archived**, lastcommit2025-03-17; maintenance risk | Hold until maintained upstream/fork and ownership are verified. Avoid committing a long-lived sampler dependency to an archived source merely for low initial effort. |
| [Verovio](https://github.com/rism-digital/verovio) | MEI engraving, MusicXML import; C++ library on Linux | LGPL3 family, exact headers and bundled fonts audited | Snapshot2026-10-02; active; high interactive editing integration cost | Candidate M9 rendering only. Does not supply complete DAW notation model, performance alignment or editing by itself. |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Offline stem-model inference CPU/GPU; Linux | MIT runtime; models/weights/training rights independent | Snapshot2026-10-05; active heavy dependency; high packaging/model cost | Evaluate at M8; isolated cancellable worker, no cloud requirement. Choose a redistributable model only after benchmark/rights gate. |
| [FFmpeg](https://ffmpeg.org/legal.html) | Video decoding/encoding and audio/container formats on workers | Build-dependent LGPL2.1+/GPL and optional nonfree components; GPL3 compatibility and codec distribution assessed per build | Established; very broad parser/codec surface; high packaging/test cost | Conditional M10. Prefer distribution shared build with recorded configure flags; prohibit nonfree builds in published GPL artifacts without explicit review. |
| [DAWproject](https://github.com/bitwig/dawproject) | Open XML/ZIP interchange specification and fixtures | MIT reference/spec code; assets/plugin states independent | Snapshot2025-07-12; format schema scope fixed by version; moderate adapter cost | Select evaluated interchange at M8. XML/ZIP bounds, semantic diff and loss report. Not a CPR/BWPROJECT implementation. |
| [OpenTimelineIO](https://github.com/AcademySoftwareFoundation/OpenTimelineIO) | Editorial timeline exchange; C++/Python adapters | Apache2; adapters/transitives reviewed separately | Snapshot2026-10-01; active; high mismatch with DAW automation/plugins | Candidate only for video interchange, not project persistence or automatic AAF solution. |

## Framework/whole-engine alternatives evaluated

| Alternative | Capability and Linux suitability | License/maintenance | Integration decision |
|---|---|---|---|
| [JUCE8](https://github.com/juce-framework/JUCE) | Strong audio/plugin abstractions, cross-platform UI and Linux support; could accelerate hosting | Current upstream AGPLv3/commercial, rather than assumed historical GPL; snapshot2026-09-28 | Do not select as core: second framework alongside Qt, coupling and license implications. A commercial JUCE license may be needed for uses that do not satisfy its open terms. If reconsidered, read exact release LICENSE fully and record product license consequences; GPL3 choice is not automatic AGPL approval. |
| [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) | Existing timeline/record/render/graph functionality, Linux through JUCE | [GPL3-or-later/commercial](https://github.com/Tracktion/tracktion_engine/blob/develop/LICENSE.md); JUCE terms independently apply; snapshot2026-10-03 | Provisional custom framework-independent core after bounded slice. Tracktion is a serious fallback, not dismissed as incapable. Before major scheduler/host investment, compare an isolated adapter against M1/M4 contracts. No evidence yet that it covers full combined parity; coupling and separate JUCE licensing create cost. |
| [Qtractor](https://github.com/rncbc/qtractor) | Existing Qt/JACK audio/MIDI workstation, useful reference for Linux workflows | GPL2-family source, exact per-file or-later terms needed for GPL3 reuse; snapshot2026-09-30 | Evaluate individual components only after file/license/ownership audit. A whole UI/session fork would bring integration/migration cost and does not prove launcher/modular/scoring parity. |
| [Ardour](https://github.com/Ardour/ardour) | Mature recording/mixing/session reliability and plugin hosting on Linux | GPL2-family/per-file review needed; active snapshot2026-10-05 | Architectural comparison and potentially audited module reuse; not wholesale adoption in this plan. UI/session assumptions and full-feature integration cost need measured evidence. |

## Release inventory policy

The final lock includes dependency version/commit, source hash, build flags, direct/transitive license expressions, notices, redistribution method, patch list, SBOM and update owner. Separate columns inventory content/model/font licenses and application code. Paid distribution preserves GPL source obligations; GPL does not license vendor samples or trademarks. A blocked library does not delete its product feature: switch implementation, budget a maintained replacement, or report the remaining requirement.

No dependency is selected merely from a permissive license or a README feature name. Present feasibility validates the Studio C++ boundary, not a full framework/dependency choice. The proposed architecture is intentionally modular so M0 can revise costly choices before broad development.

## Equipment editor reuse checkpoint

X005 uses the existing Qt Core/Gui/Widgets dependency and copied GPL equalizer editor
at initial pin `6b53056`, with equipment/catalog update `459627c` and exact
per-file revisions in the provenance manifest. No new GUI/audio framework is adopted. Curve-only C++
helpers have no Qt headers; the provisional library/profile JSON model is still Qt
GUI-side and is not the shared engine schema. Snapshot, adapted paths and later
upstream candidates are in `reuse/equipment/provenance.json`. The 1,092-entry generated
speaker catalog retains its Spinorama GPL license, source hashes and collector; full
source/data rights and packaging audit remain open. Pyle measurement arrays are excluded.
See ADR 011 and docs/17-equipment-profiles.md for integration cost and remaining gates.


## Recording discovery checkpoint

S8c adds no dependency. Existing C++ filesystem, platform file handles/leases,
libsndfile and hashing support the Qt-free bounded discovery/verification/copy
API; existing Qt owns the separate latest-request I/O worker and review dialog.
Linux nonblocking flock and Windows exclusive sharing are cooperative activity
checks in owner-controlled directories. Windows execution remains unqualified.
See [ADR 018](decisions/018-recording-discovery.md).

## Recording flush audit (2026-10-06)

Existing libsndfile 1.2.2 remains selected and licensed as previously inventoried.
The pinned sf_write_sync/psf_fsync implementations only issue an unchecked OS sync;
the recording checkpoint now retains the existing checked descriptor flush after
header/error handling and omits the redundant library call. Exact source hashes,
platform branches and source links are in [the audit](43-recording-checked-flush.md)
and its evidence. Re-audit on any dependency upgrade. No new dependency or licensing
choice; native Windows/packaging qualification remains open.


## Shared media cache checkpoint (2026-10-07)

No new dependency or license selection. The existing libsndfile decoder and
OpenSSL/Linux or BCrypt/Windows SHA-256 wrappers support one serialized shared
handle/page pool. A custom small cache avoids a second audio framework and keeps
cache policy separate from project state and callbacks. Whole-file reopen hash
cost, allocator/RSS overhead and native Windows behavior still need qualification.
See [ADR064](decisions/064-shared-media-cache.md).

## Retained Session ownership (2026-10-07)

[ADR067](decisions/067-retained-session-resources.md) uses the C++20 standard
library's shared ownership and off-audio mutex. `sc-session` links the existing
CMake Threads facility; there is no additional third-party dependency or change
to GPL-3.0-only source licensing. Platform thread/runtime redistribution still
follows the existing packaging inventory. The ledger must never be acquired or
released by an audio callback.

## Native Windows capture foundation (2026-10-08)

ADR075 selects an original Windows SDK WASAPI capture owner and an OS/Qt-free
prepared packet adapter. No additional audio framework, driver or mandatory cable
is introduced. COM/SDK import libraries (`ole32`, SDK GUIDs), existing MinGW/GCC
runtimes and codec-disabled libsndfile1.2.2 support the current native developer
fixture. These are local qualification inputs, not an end-user Windows package.
Before distribution qualify the native Qt/runtime/toolchain combination and collect
exact transitive notices/source/build configuration. Test-only existing cable
endpoints do not become installer dependencies. Native SDK source-render pumping
is fixture control work, not a selected production output engine.

## Native Windows desktop dependency checkpoint (2026-10-08)

The existing Qt dependency is now compiled and exercised natively with the
available Qt 6.12.0 MSVC x64 SDK, MSVC 19.44.35228.0 and Release `/MD`.
The SDK was copied into the DAW's own cache; the equalizer checkout was read-only.
Its matching QtBase source archive is retained and verified as SHA-256
`a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307`.
The SDK transfer manifest verifies all 4,440 Qt files. Existing Core/Gui/Widgets
license selection remains; native Test is developer-only. Exact deployed modules,
transitive notices, runtime source delivery and clean installation remain open.

Build the pinned libsndfile 1.2.2 source with MSVC/UCRT for these media callers.
The earlier MinGW/MSVCRT descriptor table cannot be shared with MSVC `_open_osfhandle`
descriptors via `sf_open_fd`. Native source hash is
`ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121`;
shared build, optional codecs/programs/examples/tests/experimental features disabled.
No dependency source or license was changed. The developer DLL is not a qualified
end-user redistribution package.

The [Qt Windows deployment guide](https://doc.qt.io/qt-6/windows-deployment.html)
requires separate third-party dependency handling and the official Microsoft
Redistributable for end-user MSVC runtime deployment. Developer SDK PATH execution
does not satisfy that gate. Do not copy individual developer CRT DLLs into a release.
[ADR077](decisions/077-desktop-native-routes-and-storage-completion.md) and
[the evidence checkpoint](98-windows-desktop-foundation.md) record the choice and limits.

## Reviewed localization refresh (2026-10-08)

ADR079 adds no library or new Qt floor. Existing Qt widgets/Linguist resources
support script/territory selection, embedded standard actions and display-only
number handling. GPL equalizer draft words are mapped to explicit DAW contexts;
110 retained review inputs have exact source hashes/revisions. Original DSP and
equipment pins remain immutable. Structural checks are not native-language review.
