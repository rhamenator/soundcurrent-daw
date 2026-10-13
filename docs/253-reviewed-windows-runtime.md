# Independently reviewed Windows runtime

Checkpoint253, 2026-10-13. The full DAW goal remains active. The native review from
PR186's desktop job passed, permitting a specific runtime pin update. This is not
an installed-app or setup qualification.

## Native facts

[Indexed original review records](../tests/results/X007/2026-10-13-reviewed-windows-runtime/index.json)
bind the original artifact and native Windows records from run38439030585, source
dc90fd6cbd64ea8e377748599bd3cddbfa0d961f. Original run38439030585 finished allfour jobs successfully: Linux161/161467.53s,
Windows native88/88321.04s, desktop20/20130.37s and cross-build. Original terminal
APIs/joblogs/Windows archives are retained separately from signature observations.
The complete123,088,035-byte original
bundle remains local in `.cache/windows-native-acceptance-prep/pr186-review/`.

- Compiler directory14.51.36231, compiler product14.51.36260.0 and file19.51.36260.0.
- Actual `include/crtversion.h` constants14.51.36244.0, SHA-256
  `7901617723f002c2d29c558002c11fa3290c14d8967841efbc316514fa405802`.
- Official Microsoft runtime14.51.36247.0,18,731,856 bytes, SHA-256
  `843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c`.
- Native Windows Authenticode status `Valid`, signer Microsoft Corporation,
  issuer Microsoft Code Signing PCA2024, thumbprint
  `1D77A9B9E8FE2075D9AD15123257FB90DB0DA4A1`.
- Version requirement passes against the actual CRT constants. The separately
  downloaded Linux candidate has the same complete byte hash.
- Runtime executed=false, installed=false; no native endpoint or VM used here.

The earlier unsupported inference equating the compiler executable patch36260 with
a required redistributable patch is contradicted by these actual component versions.
The package builder retains product/file version consistency checks, then uses the
captured CRT version and toolset directory as lower bounds. It accepts the reviewed
runtime against this exact native compiler collection. Nothing is fabricated or
borrowed from a different toolset.

## Pin update and license scope

`research/windows-preview-runtime.json` records this independent review and the
previous14.44.35211.0/hash anchor. `package_windows_preview.py` now freezes the new
version/hash, so the generated installed-runtime comparison, source instructions
and receipt use14.51.36247.0. The source notice is updated. The stable download URL
is a discovery source, not an unbounded latest-version acceptance policy; a different
future download must undergo a fresh review.

[Microsoft's supported-runtime guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)
requires matching architecture and a runtime at least as recent as the build tools.
It links the official redistributable and redistribution/license terms. This remains
Microsoft's separately signed redistributable under those terms, not copied developer
CRT DLLs or newly authored driver code. The SoundCurrent setup remains unsigned and
the application GPL-3.0-only. No signing certificate purchase is required for this
increment; source and shared-library replacement rights remain preserved.

## Next acceptance work

Prepared reusable main/helper observation scripts remain local, adapted from the
previously exercised native workflows. They require source/head/tree/deployment bytes
and observe real PIDs/exit/protocol/audio hashes. The main observer requires a normal
interactive title/window/close and records loaded module paths/hashes. They parse but
have not been executed for this revision. Fresh helper banks and actual loaded
libsndfile/runtime qualification are still needed before installer preparation.

The libsndfile binary mismatch remains quarantined; its product pin was not changed
by this runtime decision. Complete and retain the original remaining CI jobs, then
qualify the chosen dependency/application/helper bytes and prepare a matching unsigned
setup/GPL source pair. Any runtime installation or setup acceptance uses a full
independent Windows clone, after offline preparation and checking no other VM is
active; keep testing brief. Originals/template, installed Ubuntu preview and equalizers
remain unchanged. Full remaining parity and European language review stay required.
