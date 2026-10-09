# Installed Linux stretch preview

2026-10-09. This qualifies an owned synthetic workflow in the installed Ubuntu
preview from source `95cfb7d92a01abf82fa7c7f0963aec0b1fcbf698`. It does not qualify
a newer source build, physical endpoints, processing-quality parity or a release.
The frozen F/Q/C/N scope remains unchanged.

## Installation and isolation

The actual Debian package manager upgraded the prior preview
`0.1.0~preview.20261008034000.743392ece10a` to
`0.1.0~preview.20261009220811.95cfb7d92a01`. The package SHA256 is
`b959eae7715d92155f617a6d0bd79743dfc658e5e12451aded7753fb1cc59534`.
The five installed executable hashes match the prepared package, including the
stripped main executable and default sibling stretch helper. No developer SDK
was used by the installed application.

The test used a temporary volatile Ubuntu Base overlay, private network,
private PipeWire graph and Xvfb. No physical sound devices were exposed. Both
application and observed helper ran as UID 1000, with zero effective capabilities
and no-new-privileges enabled. The helper used its default installed path with
a 256 MiB ceiling. No VM was started or host package changed. Both containers
exited; the underlying image's older installed package remains unchanged.

## Actual desktop workflow and independent checks

A schema 1.12 project under a Unicode path contained owned 48 kHz stereo float
media. The selected clip anchored 8192 frames at source 17+1/2. The real installed
GUI rendered duration 3/2, independent pitch +7.00007 semitones, with preserved
formants. Review preceded explicit Apply. The saved result retains the original
raw asset/hash/span and processor identity, with a verified derived 12288-frame
asset.

Actual keyboard Undo/save restored the initial semantic project; Redo/save
restored the applied project. Normal Quit exited 0. A new installed process
reopened the same saved project without rewriting it, and its dialog displayed
the saved settings. Both actual desktop exports used the same 12288-frame range.
An independent RIFF/RF64 decoder confirmed finite float samples, stereo/rate/
duration, file/sample hashes and exact PCM equality between the derived asset
and both exports. The two export files are byte-identical. Peak 2.22680855 shows
floating-point headroom was retained; it does not establish distortion, listening
or hardware clipping performance. The second normal Quit also exited 0. Private
PipeWire node sets before/after remain identical.

A separate volatile overlay installed, removed and reinstalled the same package.
Removal deleted the installed application/helper. Hash lists for all user project
files and both exports were unchanged across removal. Reinstallation restored
the same application/helper hashes and normal-user offscreen version startup.

## Evidence and remaining gates

[The receipt](../tests/results/M2/2026-10-09-installed-stretch-preview/qualification.json)
binds the source, package, decoded results and a SHA256 manifest for the retained
[capture capsule](../tests/results/M2/2026-10-09-installed-stretch-preview/capture.zip).
The capsule preserves owned audio/project states, actual action scripts/commands,
package-manager logs, helper observations, private graphs, screenshots and the
independent capture-time verification script. It contains no installer binary,
root filesystem, credentials or personal recordings. Metadata is maintainer
observation, not authentication of an untrusted receipt.

Next qualify a current native Windows application and helper build, then exercise
its installed Unicode render/review/apply/Undo/reopen/export and upgrade/removal
workflow. The preceding Windows hosted CI receipt is not an installer receipt.
Very short spans, dynamic warp/segmented pitch, full quality acceptance and all
remaining frozen workflows stay open. Linux installed GUI delivery is useful
preview progress, not full desktop or professional DAW parity.

The portable verifier runs `python3 tools/verify_installed_stretch_preview.py`.
Its six positive/refusal cases reject altered archive identity, inflated native
scope, changed Undo state, export samples and raw samples even after the fixture
recomputes its archive manifest. These are retained-data checks, not audio replay.
