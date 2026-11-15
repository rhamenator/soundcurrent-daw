# Versioned comp processing recipes and immutable sidecars

Checkpoint 271 makes the control-side processing plan portable across save/reopen.
It does not launch a renderer, verify source media, attach a result, change the
project schema, or qualify sound quality. Original assets and active previews remain
unchanged. The full frozen-suite and platform/localization goal remains active.

## Identity and reload contract

`sc-comp-processing-recipe-v1` contains the selected outgoing/incoming clips,
retained take sources, required raw/current/nominal asset declarations and hashes,
exact mapping points, transition, maximum project blend width, processing domains,
PCM extents and permitted inner intervals. The minimal source snapshot uses the
existing versioned project codec. Referenced tracks, lanes, clips and assets are
ordered by stable IDs; unrelated routing, EQ bands, project transport and imports
are excluded. The selected clips' own processing/settings remain bound.

The complementary quintic curve and positioned resampling algorithm have explicit
identities. The executor has a fixed producer ID and a caller-supplied build hash.
This binds the requested identity; it does not verify a binary or establish that
the new producer has been implemented or qualified.

Runtime root identities and epochs are never serialized. Reload takes a fresh
context from the control owner and recomputes the immutable plan from the portable
snapshot. The saved domains, permissions, geometry and maps must agree with that
recomputed plan. Operation ID, executor and a trusted canonical SHA-256 digest are
required from the owner. The expected digest must not be learned from the same
untrusted sidecar being checked: otherwise a coherent substituted transition could
be mistaken for the owner's original selection. Whitespace can differ; canonical
recipe identity cannot. Fresh source/clip/asset/layout checks precede use against
the current session. Saving/reopening the owning project reference to this digest
is a subsequent integration task, not implemented here.

## Bounded codec and files

Codec work runs off audio. A ledger admits staging, encoded bytes, retained
recipe bytes and independently charged plan snapshots. SAX preflight bounds parser
staging, nesting and duplicate keys before building the outer DOM; the embedded
source snapshot uses the existing project preflight. The default encoded cap is
2 MiB and staging credit 64 MiB, with smaller sub-budgets for each parser. These
are conservative payload admission limits, not an exact RSS claim. Oversized
state/map/bytes, unsupported types/versions, cancellation and resource refusals
unwind ownership without project changes.

`persistCompProcessingRecipe` publishes an immutable
`media/comp-recipes/<operation>.json` under an explicit absolute root. Existing
file, flush, cooperative writer lease and no-overwrite publication infrastructure
is reused. Only this invocation's temporary file can be removed on failure.
`loadCompProcessingRecipe` uses an approved root handle, bounded reads and unchanged
file checks. Links are refused by the existing approved-media reader. No saved
path authorizes access to foreign source media. Native Windows link refusal still
needs its existing platform-specific qualification; the new test avoids creating
privileged links on Windows.

## Acceptance and provenance

[Frozen source-bound receipt](../tests/results/M2/2026-10-14-comp-processing-recipes/index.json)
binds source `2749c06204ff7929762031e586275903e903f3b7`, source files, all built static
libraries, four test executables and actual CTest process 3763208. Four selected
Linux gates pass in 2.05 seconds. The recipe gate records 618 checks over 11 cases:
9 physical, 10 post-SRC and 3 existing-media fragments. It exercises 44.1/48/96 kHz
sources, rate/fraction variants, cross-track retained sources, timeline positions
above 2^40 frames, existing processed windows, context and warp snapshots.

Actual Unicode-directory sidecar publication/reload, unchanged source state,
no-overwrite, busy writer, cancellation, stale source/context, corrupt/substituted
bytes, duplicate/deep JSON, pre-DOM resource refusal and ownership retirement are
checked. Inconsistent domains/maps/permissions also fail when the test supplies
the altered bytes' matching hash; digest checking cannot mask the need to rederive
permissions. Two copied test fixtures initially duplicated object/marker IDs;
the failed local log is retained and the fixtures were repaired without relaxing
validation. Recipe assets carry owned synthetic declarations; no media files are
admitted or renderer executed in this gate.

Regression gates retain 33,054 permission checks with actual positioned FIR and
outside-project bit preservation, 37,098 strict assembly checks, and 5,561,601
blend checks. The acceptance ZIP contains four entries, 22,939 bytes, SHA-256
`4f884b962899f24baa36094c84b5a2f135d545636bc24b7de93d45474cd4c3ad`.
Production GUI sources are unchanged; localization extraction check passes with
1,038 keys/34 partial catalogs/3,168 drafts/zero reviewed or fully UI-qualified.

## Original predecessor CI

[Run 38582997256](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38582997256)
against `35433e9ce9222fbb95f40bc4f6a6ef22ab679904` passes all four jobs: Linux 173,
Windows core cross-build, native Windows 93 plus five explicit core gates, and
Windows desktop 22. All terminal APIs, full job logs and five original ZIPs were
retained before updating the branch. [Retained review](../tests/results/M2/2026-10-14-comp-processing-recipes/pr194-permission-ci/index.json)
includes compressed originals and the independent build-input audit: 14 deployment
files and 1,560 corresponding source files match actual Windows checkout filters;
collection hashes and both media DLL builds match their pins. Main-window,
physical/native audio and installer flags remain false. This predecessor qualifies
the permission implementation, not Windows execution of the new recipe code.
Earlier failed runs and qualified banks remain intact.

## Next implementation

Bind the trusted recipe digest to an owned render request, intent/completion and
recovery inventory. Independently verify actual raw/current/nominal/retained media
and permitted PCM differences before adoption. Retain original sources, anchors,
paired atomic Undo, root/epoch staleness and cancellation/close recovery.

`ProjectAfterSrc` requires a prepared overlay shared by live/offline execution or
an explicit new representation. It cannot be encoded as an old same-rate processed
window. The controller, worker, project reference, playback, UI and native proof
remain open. Broader rate/group coverage and unresolved join/pitch/formant/phase/
transient/listening quality remain separate gates; a portable recipe is not their
substitute. Full M2–M11/F/Q/C/N/X004/X005 and all-Europe coverage are incomplete.
