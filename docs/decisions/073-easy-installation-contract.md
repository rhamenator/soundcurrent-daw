# ADR073: installation is an end-user acceptance workflow

Status: accepted product contract, 2026-10-07. Package implementation remains open.

Require graphical/native installation on supported Linux and Windows without
development tools or manual runtime assembly. Include application-menu integration,
optional desktop shortcuts, dependency handling, localized first run, upgrades,
failed-install recovery and removal that preserves recordings and projects.

Evaluate native DEB/RPM delivery and a normal Windows graphical installer against
the actual dependency/support matrix before selecting package tools and pins.
Native audio adapter and distribution compatibility must be tested independently.
The DAW Windows user-mode plan does not make VB-CABLE or a new kernel driver an
installation prerequisite. Development can proceed without a purchased signing
certificate; public distribution trust remains a separate qualification gate.

Source builds, cross-builds, `cmake --install`, source backup archives and existing
developer machines do not establish end-user installation. Start the dependency
audit alongside backend development and enforce fresh-machine, upgrade, failure,
uninstall, locale and source/license evidence at release. See [X007](../88-easy-installation.md).
