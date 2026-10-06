# S8a: portable per-channel routing intent

This implements saved input, playback and monitor **intent** for the first desktop
track. It does not connect an audio endpoint during project load, route restoration,
Prepare, Undo or Redo. Explicit Play/Record remains required. Active native connections
keep their existing routing until Stop and preparation; changing saved intent does not
silently reconnect an active graph. Full multitrack graph routing, monitor-mode
persistence, device-default policies and Windows native adapters remain required work.

## State and migration

Project writer schema is **1.1**. Decoder accepts 1.0 through an explicit migration,
and 1.1 through its strict current decoder. Unknown minor/major versions and unknown
fields fail. A 1.0 project's opaque backend/port strings, UUIDs, EQ, timing and media
are preserved; its new monitor intent is empty. Writing migrated state emits 1.1.
Older 1.0 applications cannot read 1.1; migration is not a downgrade promise.

Each track has independent `inputIntent`, `outputIntent` and `monitorIntent`.
`backendId` remains a language-independent string. Each intent retains `portIdentity`
for opaque legacy data, and adds `ports`: either an empty array for an unconfigured or
legacy route, or exactly one slot per track channel. A null slot means unassigned.
A configured slot stores `deviceIdentity`, `portIdentity`, `mediaClass` and boolean
`input` (the endpoint receives audio). Typed routes require a backend and an empty
legacy string. Capture destinations require endpoint outputs; playback/monitor
require endpoint inputs. Mixed legacy/typed identities, wrong directions and invalid
shapes are rejected transactionally.

PipeWire descriptors use exact node name, port name, media class and direction.
Numeric node/port IDs and object serials remain in the current native inventory only.
These descriptors are **not authenticated hardware identities**. Renamed endpoints
are missing, duplicate descriptors are ambiguous, and backend changes require an
explicit choice. Hardware GUIDs and device-specific matching policies need future
platform adapters and qualification; fuzzy matching is not part of this contract.

Existing project text/depth/layout/UTF-8 limits apply. Route strings are at most
4096 bytes each, and total route text in a session is at most 1 MiB. Channel layouts
remain capped at 256. The JSON text cap is still 4 MiB; a valid in-memory session can
fail encoding if its combined metadata exceeds this file cap.

## Resolution and UI

The framework-independent `matchRouteIntent` returns Unassigned, Legacy,
UnsupportedBackend, Missing, Ambiguous or Found. Found requires exactly one complete
descriptor match; it carries an inventory index, not a persisted graph ID. There is
no "first available" or default-device fallback.

The desktop restores matched choices using the **current** port IDs. Missing,
ambiguous, legacy and unavailable-backend choices display named placeholders and
cannot start playback/recording. The user can select a currently enumerated port
explicitly, including a specific duplicate while that live inventory exists. A saved
ambiguous descriptor requires another explicit choice on reopen. A successful
Create/Open increments a control-side project epoch, including reopening the same
UUID, so a previous live choice is not silently carried across project replacement.
Unassigned choices remain blank. All displayed reasons/labels use Qt translation
contexts; descriptor serialization remains language independent.

The view refreshes on model revisions and project epochs, even if an intermediate
route edit and its Undo happen between GUI polls. Cached intent equality alone is
insufficient when the GUI has already displayed a pending explicit choice. Live EQ
compatibility ignores first-track saved route intent while preserving prepared
connections; accepted/applied EQ revisions do not claim that saved route intent has
been connected. Route controls explain the Stop/preparation requirement in a tooltip.

Selecting an endpoint sends a per-channel patch to the bounded project controller.
The worker merges it with the latest accepted intent, so rapid changes to different
channels cannot overwrite earlier accepted choices through a stale GUI snapshot.
Changing backend or editing a legacy route resets incompatible slots; same-backend
patches preserve all other channels. Explicit unassignment preserves the remaining
slots. Rejected admission restores the displayed canonical intent and reports retry.
Invalid patches are rejected before committing an unrelated parameter gesture.

Route edits and scalar EQ gestures share the bounded 256-item semantic undo history.
No-op route edits do not add history or model revisions. Stable track UUIDs survive
rename/reorder. History is in memory, not serialized. Save and export barriers retain
the exact accepted model prefix. Edits accepted while a save or finalized-take
verification runs remain in the model; the old save cannot mark a newer route saved,
and take attachment preserves the newer route.

## Acceptance and limits

Core fixtures exercise all 256 channel identities, exact/partial/ambiguous matching,
wrong direction/class/backend, v1.0 migration, strict rejection, Unicode/null slots,
metadata budget, stable-ID mixed undo, bounded history and moved-directory persistence.
Controller fixtures hold the command worker while 32 channel patches queue, verify the
accepted barrier, no-op revisions, invalid/stale patches, gesture cancellation, save
while routes change, same-project reopen epoch and take-attachment edit preservation.
Desktop fixtures use owned recorded stereo media and fake endpoint adapters running
the actual prepared EQ to test route selection/save/relocation/reopen with changed
numeric IDs, missing/ambiguous/legacy/backend placeholders, explicit remapping and
Undo without active reconnection. Recording fixtures check independent input/monitor
persistence. Fake endpoints do not qualify physical device naming or native routing.

The consolidated native fixture additionally records and monitors 480,000 frames,
applies live EQ and Undo, saves/closes, moves the whole project, creates fresh inactive
nodes with the saved names, reopens/restores routes without callbacks or disk capture,
then exports all frames through the static saved EQ. Independent WAV reading checks
exact frames/rate/channels, a direct prepared-EQ replay and the native initial static
prefix before the live edit. Input removal separately preserves a verified raw prefix.
The graph observer checks system defaults, pre-existing links and owned-node cleanup.
Native ASan/UBSan/LSan uses `PIPEWIRE_DLCLOSE=false` as a diagnostic; normal dependency
unload memory remains unqualified.

[Evidence](../tests/results/SLICE-001/2026-10-06-project-routing.json) records executed
gates. Hardware alignment, Windows native workflows, arbitrary multitrack graph swaps, missing-media
UI, monitor-mode persistence and delivered European translations remain open. This
is progress toward SLICE-001, not completion of the slice or frozen product parity.

The subsequent [S8b monitoring-preference addition](27-monitoring-preferences.md)
emits schema 1.2 and explicitly migrates 1.0/1.1. The 1.1 results above remain
historical evidence of per-channel routing; latest state/preparation qualification
is recorded separately.
