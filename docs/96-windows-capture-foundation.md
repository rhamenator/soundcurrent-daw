# Useful preview checkpoint: native Windows capture foundation

2026-10-08. The Ubuntu preview in [90-preview-guide.md](90-preview-guide.md) remains
available locally. This checkpoint adds original native Windows capture code and
bounded native execution evidence. It does not deliver the Windows desktop app or
installer, or complete a frozen-reference parity family.

## Implemented

- Explicit active WASAPI endpoint inventory and read-only default-role discovery.
- A prepared, Qt/OS-independent packet adapter: ordered channels, float headroom,
  bounded packet partitioning, true/SDK-declared silence and QPC100ns→ns timing.
- A native shared event-driven SDK owner; unique-thread whole-packet acquire/return,
  pre-activation preparation, immutable first errors and joined retirement.
- Existing raw recording, checkpoints/recovery, versioned project save/reopen and
  shared-engine offline float WAV export on native Windows.
- A Windows checkpoint-sharing fix restricted to active partial-file inspection.
  Ordinary hash/media-cache reads continue to deny source mutation.

Native capture currently requires the project rate to match the selected endpoint's
mix rate. A rate mismatch is refused during preparation; resampled device/project
clock mapping remains required. See [ADR075](decisions/075-native-windows-capture-boundary.md).

## Qualified scope

Independent full-disk development VM `soundcurrent-win11-dev`, Windows
**10.0.26300.9550**, existing virtual HD Audio endpoints, MinGW/GCC13 win32 build,
codec-disabled libsndfile1.2.2. This is native Windows execution, not Wine or a
cross-build-only claim. No host speaker route, original Copperfin VM, equalizer
working tree, system default endpoint or endpoint volume was changed. The fixture
sets only its own ephemeral, nonpersistent render-session volume.

| Check | Observed result |
|---|---|
| Prepared packet contracts on Linux and Windows | Explicit {2,0} channel order; first discontinuity origin; poisoned silent backing pointer ignored; genuine zero data preserved; bounded partitions; immediate non-flat EQ acknowledgement; ten malformed/discontinuous packet cases and six invalid preparation cases refused |
| Native Windows recording/recovery tests |255 checks; synthetic480,000-frame recording, float overs retained, concurrent disk worker, prefix recovery; exit0 |
| Native Windows export tests |1,062 checks, exact shared-engine samples/ranges/tails, transactional failures, source-write exclusion, synthetic1/2/8/32/256-channel exports; exit0; these are file/CPU tests, not256-channel hardware |
| Affected Linux Debug | Packet and export2/2 in3.45s; recording/recovery1/1 in10.69s |
| Affected Linux ASan/UBSan/LSan |3/3 in16.28s |
| Native48 kHz non-silent SDK workflow | Interactive user session1, VM **Speakers (High Definition Audio Device)** endpoint, explicit loopback source;96,000 raw mono frames, save/reopen and96,000-frame non-flat EQ WAV export; no first fault, unchanged defaults, zero observed C++ allocation/free calls in prepared processing |
| Independent exported-sample calculation | Direct-form I peaking recurrence from retained raw samples, independent of production EQ code; float32 maximum error below1.5e−14 for the initial interactive run; media SHA-256 agrees with project/receipt |
| Native44.1 kHz input workflow | Unfed VM **Line In** input,88200 raw frames, preserved silence, save/reopen/export, no first fault; this validates a silent native input/state workflow, not microphone response or audible EQ |

All sample, project and native-run details belong to the retained receipt/capsule.
SDK/DLL internal allocation, hard callback deadlines, physical latency, sustained
sessions, true hardware channel counts and Windows GUI controls are unqualified.
The live bridge runs the prepared EQ; this capture fixture discards its wet output.
The demonstrated native WAV effect is offline export. Native monitoring/playback
must be implemented and qualified before claiming an audible live Windows workflow.

## Failures preserved and corrected

The first recording test could not inspect an active checkpoint because Windows
reader sharing excluded the existing writer. The restricted inspection fix makes
that native recovery test pass. A Linux-style export mutation fixture could not
write the protected Windows source; its Windows branch now verifies SDK sharing
violation and unchanged source/render rather than relaxing production protection.
Unicode project paths and file operations pass in the scoped native workflows.

The first SDK recording produced a complete take, but export setup omitted its
output directory. That original project/journal is preserved; the fixture now
creates its owned export directory. Two early build/test API assumptions and SDK
GUID link definitions were corrected; original failing source/log snapshots remain.

Initial SSH/noninteractive runs emitted complete **all-zero** takes, including an
existing cable test route. Matching exported zeros do not prove EQ. The independent
oracle rejected that purported signal result. The fixture now counts nonzero samples
only inside the accepted raw extent and refuses a synthesized-source acceptance
claim when no real signal arrived. Ordinary user silence is still valid input.
No signed-in desktop session existed in those runs. After logging into the owned
clone, a least-privilege scheduled task in interactive session1 received real audio
from the ordinary VM speaker endpoint; the owned task was removed after completion.
This distinguishes the successful context from the old runs; it is not a general
proof that every Session0 audio route must be silent. Per-app volume isolation alone
did not fix the pre-login run. Existing cable drivers were not used by the successful
speaker workflow and are not a DAW installation prerequisite.

Requesting48 kHz on the44.1 kHz endpoint produced453 frames, then a position increment
of448. Capture stopped and preserved the453-frame prefix/first-clock fault. The new
adapter refuses unqualified rate conversion; a new44.1 kHz project then completes
its native-rate workflow. No raw trimming, discarded silence, assumed latency or
silent re-anchoring was introduced.

## Next implementation task

The first final-source interactive run stopped at77,280 frames after the SDK
reported a later discontinuity and a480-frame device-position gap. Its raw prefix,
journal and first fault are retained. One unchanged-source retry completes96,000
frames; both raw/export hashes match the earlier successful interactive run, and
the accepted nonzero counter matches all92,864 nonzero raw samples. This is short
success and truthful failure evidence, not a stable/sustained timing guarantee.
Investigate native thread scheduling/MMCSS and the virtual endpoint/provider
before claiming robust Windows live operation; the cause is not established.

The [receipt](../tests/results/X007/2026-10-08-windows-capture-foundation.json)
and adjacent ZIP retain171 source/media/log payloads (10,394,040 bytes). Every
entry/CRC/archive SHA is checked before relocation. Native executables/DLLs stay
in local test archives; their hashes/manifests are retained. Read-only replay
passes the final actual nonzero export and rejects seven altered claims/media;
it also verifies the original77,280-frame SDK-fault prefix. Run:

```sh
python3 tests/windows_capture_verifier_tests.py
```

Hosted replay does not run Windows or native audio. Native binary source snapshots
are the actual build inputs retained in the manifest; the replay test/workflow
registration was added afterward and does not pretend to be a new native run.

Introduce the native Windows recording owner/device factory for the existing Qt
recording controls, plus a prepared native output owner for playback/monitoring.
Keep native-rate admission visible and preserve asynchronous project/disk work,
parameter acknowledgement, Undo, raw media and stop/close/recovery semantics.
Verify the actual Windows Qt/toolchain/runtime combination; then produce a local
compiler-free Windows installer and qualify desktop launch, recording/EQ/reopen/
export and upgrade/uninstall preservation in the independent VM.

Review the nine changed localization/profile-editor inputs in each equalizer repo
before relevant GUI reuse. Keep DAW adaptations and existing project compatibility.
All frozen F/Q/C/N, X004/X005/X006/X007 and all-Europe gates remain required.

## COM review correction

PR39 review identified that control discovery requested MTA even on an existing
UI/OLE STA. `RPC_E_CHANGED_MODE` now reuses that already initialized apartment and
never calls CoUninitialize for that failed mode change. S_OK and S_FALSE still
balance their own reference. A native Windows STA fixture repeats endpoint/default
inventory three times, observes five active endpoints, and verifies the caller's
STA type remains intact after every call; exit0. The first fixture build omitted
its explicit COM header and is preserved in the [supplement](../tests/results/X007/2026-10-08-windows-com-review.json).
No GUI/native audio run or scheduling qualification follows from this control test.
[Microsoft COM initialization contract](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex).
