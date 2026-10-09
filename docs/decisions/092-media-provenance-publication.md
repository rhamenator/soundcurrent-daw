# ADR092: bound provenance and exclusive media receipt publication

Status: selected; Linux and native Windows receipt primitive qualified through PR70.
Date: 2026-10-09.

| Alternative | Functionality / license / maintenance / integration cost | Decision |
|---|---|---|
| Adopt any verified partial file | No original occurrence/loss binding or crash completion evidence | Refused |
| Reopen saved original source roots on recovery | Recreates approval implicitly; depends on external mutable data | Refused |
| Replace an existing receipt/project with ProjectStore helper | Existing GPL helper has a different replacing-owned-state contract | Keep that helper unchanged |
| Add a new transaction/crypto/codec framework | Extra dependencies and platform/source-delivery costs | No demonstrated need |
| Original GPL typed provenance + existing JSON/crypto/decoder + native exclusive rename | Admitted immutable data, planned/verified markers, source-free checked recovery; platform/fsync/acknowledgement qualification required | Selected |

Source and selection evidence stay language-independent; original tokens are hex
and never destinations. Unknown schemas/fields refuse rather than silently lose
semantics. An intent precedes copied bytes; receipt publication is one atomic point.
A later flush failure is a visible committed outcome with unconfirmed directory
durability. No Session asset/project transaction is claimed from the marker alone.

Reuse: original admitted report/root/hash/staging code and existing nlohmann JSON,
libsndfile 1.2.2, OpenSSL/BCrypt. Native rename layout/function declarations are
implemented from official API contracts, not copied vendor implementations. No
new runtime library, external asset, driver or equalizer source. Existing notices,
source pins and GPL/LGPL/runtime-source delivery obligations remain applicable.

Costs: exact schema evolution, trusted destination namespace assumption, native
sharing/rename behavior, file versus directory flush, remote acknowledgement
uncertainty, decoder/process containment, lifecycle deadlines, installed/UI and
full import semantic/render qualification. See checkpoint 126.
