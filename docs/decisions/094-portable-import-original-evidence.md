# ADR094: owned original/loss evidence alongside destination state

Status: selected; local qualification, new native Windows revision pending.
Date: 2026-10-09.

| Alternative | Functionality, license, maintenance, integration cost | Decision |
|---|---|---|
| Save original suite/media paths | Breaks portability; implies unavailable historical approval | Refused |
| Flatten source state into today's Clip | Loses unsupported timing/processing/opaque state | Refused |
| Embed the full binary/report in project JSON | Competes with existing project/state parser admission; repeats large immutable data | Refused |
| New archive/transaction framework | New dependency/license/platform costs without demonstrated need | No new dependency |
| Portable typed descriptors + immutable owned inspection/receipt files, existing native publication and decoding | Original GPL; existing pins/notices; strict schema migration, independent verification and residue/platform qualification costs | Selected |

The original bundle/report remains authoritative evidence of inspected source bytes,
not proof of equivalent destination processing. Stable language-independent IDs bind
original occurrences to owned assets; source roots never become persistent authority.
Current source-property statuses remain unchanged until separately qualified semantic
mapping. Project save/reopen verifies complete evidence before publishing/returning
state. GUI and engine callbacks do not parse foreign projects or perform this I/O.

Existing Qt, JSON, crypto and libsndfile licensing/maintenance/Linux/Windows support
and pins are reused. Media-enabled ProjectStore verification now integrates the
original import module and existing decoder; media-disabled builds refuse this
workflow explicitly. No foreign assets/algorithms or equalizer changes are introduced.
Directory trust, independent file/project publication, quotas, storage/platform breadth
and later conversion/installed qualification are material integration costs.
See checkpoint128 for exact limits, evidence and remaining full-scope tasks.
