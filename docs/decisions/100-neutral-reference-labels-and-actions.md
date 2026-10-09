# ADR 100: Neutral reference labels and reviewed Actions updates

Date: 2026-10-09. Status: accepted owner documentation requirement; CI updates
require the normal protected merge gates.

Use Reference A/B in public descriptions, authored documentation and current
planning-manifest labels. Keep SC-DAW-BASELINE-2026-10-05, the full edition 6.1.3
and professional edition 15.0.30, all 92 acceptance workflows, quality gates,
uncertainty and source traceability. Describe the product through its workflows.
Research collectors normalize display titles; exact URLs and retrieved-byte
hashes remain original. Historical test evidence and third-party provenance
retain their recorded bytes. No history rewrite or change to another repository
is part of this maintenance.

Dependabot PR 2 resolves the checkout v6.0.3 annotated tag
`9f698171ed81b15d1823a05fc7211befd50c8ae0` to its actual commit
`df4cb1c069e1874edd31b4311f1884172cec0e10`. This is the same release and tree;
it is not an upgrade to v7. The official
[tag object](https://api.github.com/repos/actions/checkout/git/tags/9f698171ed81b15d1823a05fc7211befd50c8ae0)
and [commit](https://github.com/actions/checkout/commit/df4cb1c069e1874edd31b4311f1884172cec0e10)
establish that relationship. All four jobs retain `persist-credentials: false`
and read-only contents permission.

Dependabot PR 1 updates upload-artifact from v4.6.2 to v7.0.1 at
`043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`. Its
[release notes](https://github.com/actions/upload-artifact/releases/tag/v7.0.1)
and [action inputs/runtime](https://github.com/actions/upload-artifact/blob/043fb46d1a93c77aae656e7c1c64a875d1fc6a0a/action.yml)
show Node 24 and default ZIP archiving. Our hosted Ubuntu 24.04 and Windows 2025
runners already execute checkout on Node 24. Retain the three separately named
ZIP artifacts, file globs, hidden-file exclusion and missing-file policy. Verify
actual uploaded ZIPs and logs on the updated head before merging; direct single
file uploads are not appropriate for the multi-file desktop evidence.

Do not treat dependency update PRs as evidence of an exploitable vulnerability.
Use full commit pins, current-main CI on Linux, cross-compilation, native Windows
core and Windows Qt, resolved conversations and enforced administrator rules.
No weaker branch protection, extra workflow credentials or local VM is needed.
