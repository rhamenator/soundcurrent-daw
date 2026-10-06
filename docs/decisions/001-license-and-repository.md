# ADR-001: GPL3 and a separate DAW repository

Status: **accepted**. Date: 2026-10-05.

The user selected GPL-3.0, retaining paid distribution. Existing SoundCurrent source uses GPL-3.0-only; use that SPDX expression here and preserve notices on reuse. No dual-licensing rights are assumed for third-party contributions. Proprietary content and plugin binaries need separate rights.

`soundcurrent-studio` is the existing premium equalizer, so this new local repository is **soundcurrent-daw**. It has no remote and must not be published/pushed during this planning task. Neither equalizer checkout is modified.

2026-10-06 amendment: the owner explicitly authorized publication for backup and
selected a **public GPL-3.0 repository**. The initial no-publication restriction
is superseded for this source repository. See [backup scope](../57-repository-backup.md).

Consequence: reuse of the GPL SoundCurrent engine is a viable option. Commercial pricing does not remove recipients' GPL rights. Any future AGPL or commercial-framework dependency requires a separate decision rather than quietly changing these terms.
