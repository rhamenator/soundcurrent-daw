# ADR 081: Preserve a bounded foreign-project outline before semantic conversion

Date: 2026-10-08. Status: selected for X004 structural feasibility only.

The import contract requires native adapters, per-property loss reports and
versioned intermediate state. Start with an original C++20 byte-preserving RPP
outline. Do not interpret a recognized block name as an imported track, plugin or
timing value. All semantic properties remain unverified. The Bitwig/Cubase product
reference baseline stays unchanged.

## Evaluated options

| Candidate | Functionality and platforms | License / maintenance | Integration decision |
|---|---|---|---|
| Cockos WDL project context and line parser | Portable C++ chunk and token utilities; useful lexical reference, not a complete REAPER project schema | Cockos publishes a permissive notice with attribution/origin and altered-source requirements; exact files/transitives need their own audit. Official mirror revision `d30c30b356b2b7fb1654def8dbda90066f43061a` observed, dated 2026-10-04 | Defer adoption. Heap-owning helpers need admission wrappers; compressed contexts and broader parsing are unnecessary for this slice. No WDL code is copied or linked. |
| Existing Qt text facilities | Unicode GUI integration on both platforms; no RPP semantic schema | Existing qualified Qt modules and packaging notices remain applicable | Keep engine/import parsing framework independent; only future presentation belongs in Qt. |
| Original bounded C++ outline | Owned raw bytes, line/block offsets, limits and cancellation; Linux/Windows build without Qt | New GPL-3.0-only project code, maintained here; synthetic qualification only | Selected narrow foundation. Additional grammar/version/corpus qualification required before conversion. |

Primary references: [Cockos WDL and official mirror link](https://www.cockos.com/wdl/),
[pinned project context interface](https://github.com/justinfrankel/WDL/blob/d30c30b356b2b7fb1654def8dbda90066f43061a/WDL/projectcontext.h),
[pinned line parser](https://github.com/justinfrankel/WDL/blob/d30c30b356b2b7fb1654def8dbda90066f43061a/WDL/lineparse.h),
[pinned chunk implementation](https://github.com/justinfrankel/WDL/blob/d30c30b356b2b7fb1654def8dbda90066f43061a/WDL/projectcontext.cpp).
The official [REAPER guide register](https://www.reaper.fm/userguide.php) now links
guide 7.81; the previously assessed [7.74 guide](https://dlz.reaper.fm/userguide/ReaperUserGuide774.pdf)
remains a project-file overview, not an exhaustive field specification. Neither
guide nor utility code establishes our adapter's native compatibility envelope.

## Consequences

Preserve unknown state exactly; no media/path resolution or plugin/script loading.
Explicitly refuse quoted delimiters and unqualified block syntax rather than
silently treating them as ordinary data. The utility parser demonstrates that
more chunk syntax exists: this first inspector does not claim all RPP grammar.

Require a shared off-audio ResourceLedger for document and transient stack
payload. Trusted limits can increase together with budget; they are not read
from foreign data. Preserve a single immutable owner and its offsets, retire
memory before credit and check cancellation per bounded line. This is payload
accounting, not an allocator/RSS or operating-system sandbox guarantee.

Before exposing imports to users, add a bounded isolated loader/worker and
persistent structural/loss report, source provenance, rights-cleared native
fixtures, semantic mappings, media approvals and preview/transactional UI.
Unknown native formats remain required investigations; this choice does not
replace Bitwig/Cubase native import with an exchange or media-only workflow.
