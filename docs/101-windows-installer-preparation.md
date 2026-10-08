# Windows installer and source-pair preparation

This is a bounded local preview preparation. Compiler-free installation, upgrade,
removal and installed native recording still need acceptance on the independent
clean clone. Full Windows parity remains open.

## Package contract

`tools/package_windows_preview.py` consumes a clean committed checkout, retained
native input hashes, actual main-executable launch/normal-close evidence, and the
exact deployed executable identity. It checks ZIP membership, size bounds,
duplicates after native separator normalization, symlinks and all payload hashes.
Developer CRT DLL copies are refused. The local deployment contains Qt 6.12.0
Core/Gui/Widgets, Windows platform/style and GIF/ICO/JPEG plugins plus matching
MSVC/UCRT libsndfile 1.2.2. Qt Test and developer SDK files are not product payload.

The Unicode NSIS installer targets Windows 11 x64. It creates an owned per-user
preview directory, Start-menu and desktop shortcuts and an Apps uninstall entry.
Different preview builds coexist; automatic replacement/cleanup of older previews
is not claimed. Unowned nonempty folders are refused. Partial owned extraction
can be retried. Removal preflights loaded executables/DLLs and deletes only the
generated fixed-file list, retaining projects, preferences and unrelated files.
No recursive project deletion or system audio/driver changes are performed.

Microsoft's separately signed x64 runtime installer is bundled unchanged, version
14.44.35211.0, SHA-256
`cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b`.
Its Authenticode status was Valid, signer Microsoft Corporation. It runs only
when the installed runtime is insufficient; its elevation is separate from the
per-user application. Cancellation/refusal aborts setup. No forced reboot.
The SoundCurrent wrapper is unsigned pending the owner's signing certificate.

The package is paired with the exact Git source archive, pinned QtBase 6.12.0
and libsndfile 1.2.2 source archives, generated checksums and build/deployment
receipts. Notices, dependency license texts and Qt's supplied SBOM accompany the
payload. Shared libraries remain replaceable. Qt sources: SHA-256
`a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307`;
libsndfile sources:
`ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121`.

## Evidence and boundaries

The first owned installer prototype compiled with NSIS 3.10-2. Its metadata was
test-only; it is not a source-paired release. Ten hostile deployment cases were
refused, including traversal/drive/absolute names, normalized duplicate paths,
missing files, hash/size/oversize errors, symlink and developer CRT injection.
Normal Windows ZIP separators were accepted. The checked-in tiny-payload test
now rejects 14 mutations, adding reserved Windows devices, trailing dots, empty
components and case collisions; hosted checks run it without executing setup. Later template changes still need
the completed source-pair build and installed runtime tests.

The current actual native main executable launches and closes normally in limited
interactive Windows session 1: 2,275,328 bytes, SHA-256
`7dab2d068a9c1a5f3e715167c839f172fb60eefdb39f2136211b1f831222b559`.
Its runtime came from the developer SDK PATH; this does not qualify deployment.
201 refreshed/retained native code/resource input hashes match current source.
The rendered-unit correction passes Linux and native Qt tests separately.

[Preparation receipt](../tests/results/X007/2026-10-08-windows-installer-preparation.json)
records those checks. A full independent clone of the pristine template was
created as `soundcurrent-daw-install-win11`; it has no backing disk and passes
qemu-img check. Normal console login works; remote SSH is not configured/reachable.
Use isolated test media for installation/testing, preserving the template and
original Copperfin VMs. No installer has run in that clone yet.

Next: produce the clean committed installer/source pair, install from owned test
media, qualify actual installed main/shortcuts/native recording/EQ/project/WAV,
then test removal/reinstall and failure paths. Installer localization and native
language review, seamless upgrades, physical devices, sustained recording and
all frozen F/Q/C/N requirements remain open. No binary release is uploaded.

Primary deployment guidance:
[Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html),
[Microsoft supported runtimes](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170),
[NSIS scripting](https://nsis.sourceforge.io/Docs/Chapter4.html).
