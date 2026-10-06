# Contributing

SoundCurrent DAW is an early professional audio workstation implementation. See
[README.md](README.md), [the roadmap](docs/04-roadmap.md), and
[the frozen parity matrix](docs/01-parity-matrix.md) before proposing changes.

1. Open or link an issue for substantial changes. Explain the workflow, acceptance
   criteria, platform effects, and dependencies.
2. Keep pull requests focused. Preserve existing project compatibility and stable
   IDs; document schema migrations and recovery implications.
3. Run the documented build and relevant tests. Report failures and untested
   cases. Compilation, synthetic tests, and native device qualification are
   separate evidence.
4. Keep allocation, disk I/O, GUI work, logging, and blocking locks out of real-time
   callbacks. Test bounded queues, latency, retirement, and overload behavior when
   changing the engine.
5. Preserve copyright/license notices and record provenance for borrowed code,
   equipment data, content, and dependencies. Contributions must be compatible
   with GPL-3.0-only; do not submit proprietary assets without distribution rights.
6. Use translation-ready strings and language-independent stored state. Do not
   count untranslated catalogs as language support.
7. Exclude secrets, personal recordings, generated output, and unrelated changes.
   Report vulnerabilities through [SECURITY.md](SECURITY.md).

Native audio fixtures are explicitly opt-in. Use owned routes and run them
serially after builds and other tests finish. Preserve user audio routes and
retained failure evidence; do not run physical or audible tests unintentionally.

Equalizer reuse is copied and adapted within this repository. Preserve its
provenance; do not change separate equalizer checkouts as a side effect of a DAW
change. Local VM credentials and testing policies belong outside source control.

No contributor agreement or transfer of copyright is required. Maintainers review
changes; an open issue or pull request does not promise an implementation date.

`main` is protected: use a feature branch and pull request, pass both required CI
jobs, update against current `main`, and resolve review conversations before a
squash/rebase merge. The rules apply to administrators too. See the
[branch-protection policy](docs/58-repository-branch-protection.md).
