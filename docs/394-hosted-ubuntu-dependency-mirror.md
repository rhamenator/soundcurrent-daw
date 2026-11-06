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
Ubuntu archive URI tokens to the primary Ubuntu HTTPS archive in legacy source
lines and Deb822 URI fields. Suites, components, Signed-By keys, options,
security/custom repositories and comments are preserved. Source candidates must
be plain unshared files; all candidates validate before any write. The helper
retains file owners/modes and emits before/after SHA256 values without copying
configuration contents into logs. Its JSON receipt is included in the existing
CI evidence archive. It has no local-host invocation or root-override CLI.

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
