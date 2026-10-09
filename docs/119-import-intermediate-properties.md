# X004: owned import intermediate representation

Date: 2026-10-09 UTC. Frozen product baseline unchanged.

`sc-reaper-import` adds an original framework-independent C++20 intermediate
representation. It owns the existing exact-byte structural document, object
hierarchy, stable property IDs, decoded original values and per-line evidence.
This is the first source-property layer. It does not yet convert a project or
change the installed previews, current File-menu inspection protocol or session.

## Value and evidence contract

Schema version1 uses language-independent numeric property IDs. Object ordinals
identify an object in these exact source bytes, not a SoundCurrent UUID. Original
track/item identity tokens remain bytes. Names, filenames and unknown encodings
remain literal byte ranges borrowing the unique source owner. No normalization,
path approval, escaping, UUID conversion or Unicode validity is inferred.

| IDs | Original values | Units / limits of interpretation |
|---|---|---|
| 1–2 | Project sample-rate value and enabled flag | Hz and0/1; disabled does not select an effective device rate |
| 3–7 | Track identity/name/gain/pan/channel count | Raw identity/name; linear gain; source pan scalar; positive integral channel count, not a qualified layout |
| 8–12 | Item identity, position, length, fade-in/out lengths | Raw identity; seconds; fade shapes/automatic fades remain opaque |
| 13–17 | Take name, item gain, take gain/pan/source offset | Raw name; separate linear gain layers; source pan scalar; seconds; single-take envelope only |
| 18–19 | Take rate and pitch | Source ratio and semitones; processing unsupported even for unity/zero |
| 20 | WAVE source filename | Literal reference bytes; media is not opened, resolved or copied |

**Preserved** means the original value or structural line is retained/decoded.
It does not mean destination behavior or audible equivalence. **Converted** is
reserved and never emitted by this layer. **Unsupported** identifies known work
still needed, including rate/pitch, MIDI/other source types, sends, tempo and FX.
**Missing** records required absent fields with no value or invented default.
**Unverified** records unknown flags/state, malformed tokens/numbers, unexpected
field shapes, duplicates and ambiguous take selection. Every original line and
block extent remains available, including tails of partially decoded lines.

The native stereo witness demonstrates that item `VOLPAN` contains item gain,
take pan and take gain as separate fields. They are not collapsed into track gain.
Fades preserve duration only; the remaining twelve fields are unverified. Track
gain/pan retain the other flags as unverified. Sample-rate flags, pan laws,
stretching modes, tempo attachment, looping, polarity, automation and routings
must be handled before claiming conversion/render equivalence.

TRACK/ITEM/SOURCE objects are recognized only in the direct root/track/item
hierarchy. A plugin-shaped nested TRACK or NAME cannot masquerade as a real track
or rename one. Repeated fields retain every occurrence, mark duplicates and
refuse a first/last-value lookup. Multiple sources, TAKE markers/blocks or no
direct source mark single-take selection unverified. Unknown source types retain
their type/state and never fabricate a filesystem media reference.

## Ownership, bounds and cancellation

Parsing is off audio and has no Qt, media, plugin, device or filesystem work.
Trusted input/line/depth limits feed structural inspection; object/property/token
limits bound the additional representation. These are resource admission limits,
not a fixed product track limit. Larger trusted limits need matching admission.

The shared ledger reserves conservative vector capacity before allocation, checks
actual capacities against that charge and returns scratch credit after scratch
retirement. Lease members precede their banks. Owners are move-constructible and
immutable; copying and move assignment are disabled. Source and representation
leases can outlive ledger facades and retire on the worker. Cancellation is checked
between bounded lines/objects/properties, including after representation admission.
This is declared payload accounting, not exact process RSS or hard OS sandboxing.

The original fixed32-slot lexer accepts double, single and backtick delimiters,
retains literal contents and refuses unterminated or adjacent quoted-token forms.
It does not copy/link WDL. The [pinned public WDL lexer](https://github.com/justinfrankel/WDL/blob/d30c30b356b2b7fb1654def8dbda90066f43061a/WDL/lineparse.h)
and [official native APIs](https://www.reaper.fm/sdk/reascript/reascripthelp.html)
provide lexical/property context; the existing original native-writer corpus
provides the actual save/reopen witnesses. Neither is a complete RPP schema.
Strict finite, fully consumed locale-independent numeric parsing marks unknown
forms unverified rather than imitating permissive vendor conversion.

## Acceptance and limits

Linux Release and ASan/UBSan each pass885 checks of this layer (leak detection
disabled), including all seven frozen original writer projects. Readbacks verify
track names/identities/gain/pan/channels; item position/length/fades; separate gain
layers; take name/pan/offset/rate/pitch; and relative audio-source references.
MIDI-specific offset flags, MIDI payload and processing remain unqualified.
Failure tests cover duplicate fields, invalid/nonfinite/partial/locale numbers,
unknown sources, opaque nested objects, missing fields, ambiguous takes, mapped
token/objects/properties/memory limits, moves, facade lifetime and mapping-stage
cancellation. The separate original hash/byte-inventory tests remain required.
All five selected related Release tests pass; MinGW compilation passes without
executing Windows binaries. New native MSVC execution is pending.

PR61's corrected exact head `96ffb4e` passes76 Linux tests and13 selected MSVC tests
. Its Windows corpus inspector passes6,013 checks against the
Linux-generated bytes. The original Lua checkout failure and repaired checkout
proof remain retained. This is structural inspection on Windows, not a Windows
source writer, new property-layer qualification, Windows Qt or installed workflow.

Evidence: `tests/results/X004/2026-10-09-import-properties`; decision:
[ADR085](decisions/085-owned-import-property-layer.md). No local VM, original VM,
equalizer worktree, user audio route or installer changed. Full functional,
quality, content and native-project parity remain active/incomplete.

## Next concrete implementation task

Run this model inside the existing isolated inspector and publish a versioned
numeric/range/property report. Independently validate parent ownership, counts,
hashes, property IDs/types/units/statuses and complete unknown-byte coverage before
displaying the source-property/loss preview. Preserve async cancellation and
portable inspection Save/Open; do not run foreign code or resolve dependencies.

Then add approved media roots and missing-media choices, explicit new-project
mapping, Undo/reopen and independently aligned native/source render comparisons.
Track/frame timing, gain/pan/fade conventions, multiple takes, MIDI, tempo,
routes, automation and plugin/container state require their own acceptance cases.
Native Bitwig/Cubase and every other registered native/exchange family remain
required, with separate version envelopes and rights-cleared corpora.

## Line-evidence review follow-up

A mixed `PLAYRATE nan ...` line initially let a later unsupported pitch overwrite
its malformed-rate line evidence. Explicit precedence now keeps malformed fields,
duplicates and ambiguous selection ahead of unsupported processing. Individual
property records stay intact. The added regression passes with890 Linux Release
checks; its separate receipt pins the tested source and scope. Original885-check
evidence remains historical. New hosted execution of this repair is pending.
