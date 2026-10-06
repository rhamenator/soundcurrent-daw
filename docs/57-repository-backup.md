# Public source backup and Windows copy

Date: 2026-10-06. The owner explicitly authorized publication and selected a public
GPL-3.0 repository: **rhamenator/soundcurrent-daw**. This supersedes the initial
planning-only restriction on pushing this repository. Releases and qualified
installers remain separate work. The equalizer repositories are unchanged.

## What is backed up

GitHub receives tracked source, complete reachable Git history, planning,
dependency/license notices, reuse provenance and committed test receipts.
`.cache/`, builds, generated recordings and local credentials are excluded.
Ignored native failure executables/media remain local and are not backed up by a
source push. Keep separate backups of personal sessions/media and any local
diagnostic artifacts that need to survive a drive failure.

Source updates now go through feature branches and required-check pull requests;
`main` has [verified protection](58-repository-branch-protection.md). Direct pushes
to `main` are not the backup workflow.

## Windows source checkout

With Git installed on the intended Windows machine, in PowerShell:

```powershell
git clone https://github.com/rhamenator/soundcurrent-daw.git
Set-Location soundcurrent-daw
git log -1 --oneline
```

Use `git pull --ff-only` for later updates when the checkout is clean. GitHub's
**Code → Download ZIP** provides current tracked files without Git history. The
repository uses explicit source line-ending attributes for Windows/Linux copies.

A locally prepared `soundcurrent-daw-<commit>.bundle` additionally contains Git
history and can be transferred without a network checkout:

```powershell
Get-FileHash .\soundcurrent-daw-<commit>.bundle -Algorithm SHA256
git clone .\soundcurrent-daw-<commit>.bundle soundcurrent-daw
Set-Location soundcurrent-daw
git remote set-url origin https://github.com/rhamenator/soundcurrent-daw.git
```

Compare the bundle hash against its separately supplied manifest before restoring.
The bundle and source ZIP are copies of development source, not an installable
Windows audio application. Native Windows audio/desktop qualification and
installer deployment remain open. A purchased signing certificate is not required
for a source checkout or these unsigned development checks; see
[the signing decision](49-windows-signing-budget.md).

The Windows destination must be identified before transfer. For local Copperfin
VMs, follow the external owner policy, preserve originals and credentials, and
use independent clones for risky/system-changing work.
