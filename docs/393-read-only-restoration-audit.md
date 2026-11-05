# Read-only restoration evidence

`inspectAssetRestoration` runs on a serialized control/I/O worker. The caller
selects an absolute project root, expected project and saved asset identity,
operation UUID, media byte limit, shared resource ledger and project budgets.
No journal chooses a root or authorizes access to an original source.

The audit pins the directory through `ApprovedMediaRoot`. It reads `project.json`
through a native relative plain-file handle, retains its exact bytes and SHA256,
and admits an immutable decoded Session with encoded, parser and state banks
reserved before allocation. This is passive manifest admission: it validates the
schema and model without opening unrelated assets. `ProjectStore::load` remains
strict and unchanged. A passive manifest is not a fully qualified open project.

The required operation `intent.json` must be strict Planned evidence matching
the independently expected asset and operation. Optional `receipt.partial` or
`receipt.json` must be strict Published evidence with the same metadata, byte
extent and historical context as the intent. Both receipt names together are a
conflict. Old epoch/revision/request values are compared only for historical
consistency; they are never supplied as current controller authorization.
Malformed, unreadable, oversized, mismatched or linked records refuse admission.
Only the native MissingMedia error produces an absent observation.

The canonical saved media path and operation `media.partial` are independently
opened beneath the pinned root. Each present file needs the exact intent extent
and a complete streaming SHA256 match. The result reports missing or verified
canonical bytes, missing or verified staged bytes, and missing/partial/final
receipt observations separately. A partial receipt can precede publication; a
final receipt can survive later media loss. Even matching canonical bytes plus a
receipt do not establish that this operation caused publication. Both media
names can be observed without attributing their cause. No boolean claims a
historical publication or a completed rollback.

The move-only result retains the manifest, intent and native file owners in the
same ledger scope. `reverify` checks pinned files and hashes, reopens every exact
relative name and checks its visibility, extent and hash, with directory stamps
before and after. Missing is an observation at read time, not durable absence.
This uses the existing trusted destination-writer contract. It is not an atomic
namespace snapshot against hostile same-user writers, a durable commit proof,
a finite-audio decoder check or permission to clear a recovery latch. Native
Windows pinned handles deny write/delete sharing; Linux verifies native file
identity/change stamps around reads. Cancellation is cooperative around native
I/O, hashes and manifest decoding, not a hard OS deadline. Declared banks and
handle limits are admission accounting, not allocator/RSS guarantees.

The audit has no writes, rename, source reopen, Session/history mutation,
autosave permission or latch-clearing API. Future controller reconciliation
must own the real I/O transaction, bind its fresh accepted model/root, reverify
its evidence and make an explicit policy decision about each observation. A
later UI must expose unresolved/mismatched evidence without phantom rollback.
Full P084 recovery and the localized recovery workflow remain open. The frozen
Windows preview producer, package inputs and existing quality gates are unchanged.

## Qualification

`restoration-native-audit` uses independent 52-byte Signed16 WAVE bytes and
Unicode directories. Actual native staging creates prepared-only,
pre-canonical-rename, post-canonical-rename/pre-final-receipt and complete cases.
It checks full directory/file byte maps before and after audit, an unrelated
missing asset with strict open still refusing, final receipt with missing media,
corrupt canonical bytes/records/manifest, mismatched saved identity/context,
conflicting receipt names, missing intent, root/operation links, cancellation,
budgets, ledger scope, move retirement, native handle retirement and subsequent
name visibility changes. Linux mutation must be detected; Windows mutation must
be blocked while the native pin is held. Existing restoration and strict record
regressions remain required. CI includes this test on Linux and native Windows;
syntax checks alone never qualify native behavior.
