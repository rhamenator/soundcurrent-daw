# ADR 080: Owned per-user Windows preview slots with source delivery

Date: 2026-10-08. Status: selected for bounded local preview preparation.

Use the available NSIS Unicode compiler for an ordinary Windows 11 x64 installer.
Deploy shared Qt/MSVC media dependencies with exact hashes and independent official
Microsoft runtime setup. Keep the application unelevated. Retain exact application
and dependency source alongside unsigned local artifacts.

Different early preview builds use separate owned slots. This avoids overwriting
a previous usable preview while testing a new one. It does not replace the final
seamless-upgrade requirement. Uninstallation uses a generated fixed-file list and
loaded-binary preflight; project/preferences deletion is never inferred from the
application directory. Refuse unowned nonempty destinations.

Preparation/compilation and development-SDK execution are separate from actual
clean-installed acceptance. Qualify on a full independent pristine-template clone
with no compiler/Qt SDK dependency. Installer language coverage, failure recovery,
upgrade consolidation and native physical workflows remain gates. See
[checkpoint 101](../101-windows-installer-preparation.md).
