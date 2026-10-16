# Linux and Windows comp workflow previews

Checkpoint 319, 2026-10-15. Both local package candidates use source
`258016450f93ebbecd2d62fa8ee6f9f9e60964e0` and sequence `20261015124133`.
They include the original-context paired and short-edit desktop integration.
The existing installed preview has not been replaced. No binary release was
uploaded, and full DAW or processing-quality parity is not established.

## Prepared artifacts

| Candidate | File | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Ubuntu 26.04 amd64 | `soundcurrent-daw_0.1.0~preview.20261015124133.258016450f93_amd64.deb` | 7,096,582 | `ab1cf791db2b17087d26062dbe58e79e8264a88f690cc9fa352fc27f182a8cf5` |
| Unsigned Windows x64 | `SoundCurrent-DAW-20261015124133-258016450f93-x64-setup.exe` | 30,452,722 | `79d77416777ab9d6cd090120d8b73d4cd45d601e00e506db7b66e025d2774cd0` |

Maintainer-local folders are `.cache/p319-package-20261015124133/` and
`.cache/p319-windows-package-20261015124133/`. Each contains delivery notes,
checksums and its corresponding application source. Windows also supplies the
reviewed QtBase and libsndfile source archives. The approximately 2.45 GB
application source archives include historical test evidence; they are separate
from application download sizes.

The Linux source SHA-256 is
`6e7dbb7cb9bb5950f26a47efe97253b1ffaf60ce7f1e9987537ea6b9b7419772`;
the Windows source SHA-256 is
`2acc119f2b6cff4ae04e8325ebb29a7ddab5ef7958bebdec46c2444e58d73ba2`.
Both contain all 8,045 tracked files. Streaming verification checked archive
hashes, gzip integrity, membership and source contents. Windows' 1,790 build
inputs match the actual native snapshot. Other contents match their Git blobs
or Git's configured export filters. The source archives differ because of
Windows checkout line endings; neither has been relabeled to a later commit.

## Native Linux build and package checks

The existing Release build was refreshed with PipeWire enabled, using one
low-priority build worker. All 25 selected tests passed in 163.79 seconds. The
test inventory and all captured source/executable hashes were checked before
and after execution. This is a focused package gate, not a new full-suite run.

The paired and desktop short-edit workflows each exercised three adoptions,
five actual workers and mixed retained media. Each checked 320,340 worker PCM
samples and 130,576 live/export samples. The short crop is 4,054 frames rendered
from 34,828 frames of original context with baked pitch retained. All six exports
are byte-identical to the earlier qualified Linux Release results. The desktop's
maximum observed render stage was 8,199 ms under its unchanged 30,000 ms limit.

Private instances of the installed PipeWire infrastructure qualified:

- 480,000 playback frames, live EQ edits and undo, with zero missing frames and
  zero live/offline sample difference.
- 99,072 recorded raw frames, durable terminal state, save/reopen and admitted
  playback of 8,192 frames with explicit 257-frame leading silence.
- Zero observed callback allocations, frees or blocking locks; owned nodes only.

The DEB's extracted payload matched the staged bytes and modes. The actual
packaged main executable opened an owned Unicode project, saved, quit normally,
reopened, saved and quit normally again. Media, the source fixture and the
installed application launcher remained unchanged. Its rendered window was
visually inspected. These checks used private test routes, not physical devices.

## Original CI and native output review

[Run 38733355584](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38733355584)
passed all 12 jobs. Its original 12 full logs, 11 artifact ZIPs and 270 PNGs were
retained, with artifact sizes, API digests and ZIP CRCs verified. The five Linux
partitions cover 221 unique tests; native Windows banks passed 43 and 93 tests,
and the desktop groups passed 22, 6 and 3 tests. Import/localization and cross-build
checks also passed. The native core job's merge checkout has the same source tree
as the candidate; desktop deployment collection used the exact candidate commit.

Detailed reviews checked legacy contracts, renderer and worker corpora, repeated
desktop edits, original-context paired adoption, source/dependency collection,
and exported PCM. Four of six new native paired/short-desktop exports are
byte-identical to Linux; the other two differ by at most
`1.1920928955078125e-7`. Nine ordinary repeated-context exports have a maximum
cross-platform sample difference of `2.384185791015625e-7`. These measurements
remain distinct from processing-quality admission.

An audit initially assumed that only the 15 adopted workers could leave complete
audio artifacts. The original evidence also contains two completed candidates
refused after ancestry corruption and two canceled at Apply on dialog close.
The corrected audit verifies all 19 artifacts and checks those failure outcomes.
The original audit failure is retained; no product/test change or CI rerun was
used to obtain the passing review.

## Exact Windows deployment qualification

The independent development clone was the only running VM. Its standalone disk
and identity were checked first. The original VMs, pristine template, equalizer
repositories and physical audio routes were untouched.

The clone verified the reviewed Microsoft runtime's hash and native signature,
installed it successfully, and checked all 1,790 source inputs and 15 deployed
files. Actual deployed import, WAVE-validation and checked-copy helpers passed.
The deployed stretch helper passed 385 driver checks and 109,597 verifier checks;
protected stretching passed 519 checks, 33 worker processes and 21 renders.
The native helper checks took about 115 seconds in total; no compiler was run
inside the VM.

The actual deployed main executable opened with the default Windows style in
interactive session 1, PID 7452, and closed normally with exit 0. Its screenshot
was inspected. No forced termination or style workaround qualified this run.
The owned scheduled task was removed, no owned deployment processes remained,
and the VM shut down before installer compilation began.

The first host packaging attempt correctly refused native input byte differences:
the build snapshot used Windows line endings. Packaging then ran from an isolated,
clean checkout of the same commit with `core.autocrlf=true`. Every native input
matched before the unchanged producer ran. This did not edit the main checkout,
rebind a receipt, or waive a source check. Both the refused attempt and successful
producer records are retained.

## Evidence and next acceptance task

The [portable evidence index](../tests/results/M2/2026-10-15-comp-preview-delivery/index.json)
describes the content-addressed archive. It contains the current package receipts,
native Linux/Windows observations, source checks, full CI logs, screenshots and
detailed audits. Original complete CI ZIPs remain locally; prior unchanged
corpora also remain in earlier committed evidence. Product executables and
installers are not included in this evidence archive.

Next, qualify this exact Windows installer in the independent SDK-free acceptance
clone: install, launch, open a Unicode project, exercise the short-edit workflow,
save/reopen/export, preserve the older preview, and verify uninstall ownership.
Then qualify an updated Linux Applications launcher against the same candidate.
Keep each VM session bounded and run only one VM at a time.

Clean-system installation, installed hardware workflows, sustained recording,
processing quality, broader layout/accessibility and European language
qualification remain open. The professional feature backlog is unchanged.
