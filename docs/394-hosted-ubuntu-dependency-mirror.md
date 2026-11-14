# Hosted Ubuntu dependency downloads

Parent run42148681445 repeated the Linux shard1 outer-budget cancellation seen
in runs42111727629 and42124874344. In the parent, dependency installation took
about26.7 minutes, then compilation succeeded in about12.1 minutes. The full
shard was progressing when the45-minute job budget ended. Earlier attempts
ended at different cases after26–30 minutes of dependency downloads. Their
logs show repeated Azure Ubuntu mirror transfers/ignores for libLLVM/libClang
and Qt packages, rather than a per-test timeout. The once-retried PR229 run
completed all47 assigned cases without a source or deadline change.

The Linux synthetic jobs now qualify a small configuration helper, then run it
only inside the GitHub-hosted Ubuntu24.04 runner. It changes only exact Azure
Ubuntu archive URI tokens and the reviewed runner URI
`mirror+file:/etc/apt/apt-mirrors.txt` to the primary Ubuntu HTTPS archive in legacy source
lines and Deb822 URI fields. Suites, components, Signed-By keys, options,
security/custom repositories and comments are preserved. Source candidates must
be plain unshared files; all candidates validate before any write. The helper
retains file owners/modes and emits before/after SHA256 values without copying
configuration contents into logs. Its JSON receipt is included in the existing
CI evidence archive. It also records approved primary URI and replaced mirror-list
URI counts. No approved primary URI after rewriting fails before any write;
already-primary input is an explicit admitted idempotent case. The helper never
opens a mirror-list file. It has no local-host invocation or root-override CLI.

APT uses two retries and30-second HTTP/HTTPS connection/data inactivity timers.
These are not a hard whole-download or whole-install deadline. The45-minute job
budget, package list, authentication/signature policy, full five-shard test
inventory, per-test deadlines, build parallelism, failure retention and final
merge gates are unchanged. Failures still fail the job. There is no automatic
whole-job retry or third retry of the old cancelled parent run. The new exact
head must qualify the actual hosted setup and full Linux/Windows matrix before
integration; isolated source-rewriting tests do not establish mirror throughput.

The narrow CI helper does not change the DAW, its dependency versions, release
packages, frozen preview, controller reconciliation or recovery policy. Those
product requirements remain open as recorded in the checkpoint.

The independent configuration fixtures exercise legacy and Deb822 sources,
multiple URI fields, paragraph boundaries, comments/CRLF, exact URL boundaries,
foreign/security repositories, credentials left untouched, key/options/mode
preservation, idempotence, symlink/hardlink/non-file refusals before changes,
non-CI CLI refusal and the unchanged workflow gates. No host APT configuration
or packages are modified by local qualification.

References: [Ubuntu APT HTTP transport options](https://manpages.ubuntu.com/manpages/noble/man1/apt-transport-http.1.html),
[primary Ubuntu archive](https://archive.ubuntu.com/ubuntu/).


The first helper head6786cb66 missed the runner mirror-list indirection. Its
actual hosted receipt reported changedFiles0; APT then loaded its144-byte
mirror list, selected Azure and spent24.2minutes in setup. Linux shard2 later
hit the outer budget while tests were progressing. This original failure and
receipt are retained, not treated as a qualified mirror switch. The repair adds
that exact URI and explicit no-op admission checks. Other mirror+file paths
remain untouched and grant no file access. The reviewed runner source is
[configure-apt-sources.sh at42d27e4](https://github.com/actions/runner-images/blob/42d27e4a2c1232e5640a88ed42a974c8918ecd28/images/ubuntu/scripts/build/configure-apt-sources.sh);
the actual hosted log independently confirms the same mirror-list reference.
Fresh exact-head hosted execution is still required before integration.
