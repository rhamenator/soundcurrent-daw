# Explicit restoration helper package profile

The native restoration-image collector now emits `package-profile.json` with
profile `restoration-workers-v1`. The same implementation binds all four
production roles to the captured repository commit/tree, source hashes, native
image identity, byte count and executable hash. Build configuration, the CMake
cache hash and the installed positive-controller receipt hash are also bound. Linux payload destinations are
`usr/bin/<executable>`; Windows destinations are the corresponding flat `.exe`
names beside the application. Unexpected roles, fields, paths, source bindings,
platform identities, aliased images and excess bytes are refused.

Future package admission must cross-check this profile against the matching
collection and its positive process/image/source evidence. A profile alone is
not native execution proof.

The collection receipt records the profile file's byte count and SHA-256.
Collection still requires the existing actual installed positive controller
workflows, immutable pre-build source capture and exact native-image/source
verification. Synthetic mutation tests exercise refusals; only real Linux and
Windows collection qualifies native observations.

This is a helper-only payload plan for subsequent explicit package integration.
It leaves existing preview inventories and historical delivery receipts intact.
It does not qualify dependency distribution, a standalone bundle, an installer,
an installed GUI workflow or broad recovery. Package builders must still admit
the selected profile, preserve source/license provenance, verify dependencies
and qualify each supported native installation lifecycle before delivery.
