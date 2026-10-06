# Main branch protection

Date: 2026-10-06. The owner requested protection compatible with continued agent
development. These settings were applied to `rhamenator/soundcurrent-daw` and read
back from GitHub; no existing protection/ruleset was present before this change.

`main` requires:

- A pull request. Direct pushes are blocked, including for administrators.
- Both **Linux desktop and synthetic tests** and **Windows core cross-build (no
  native runtime claim)**. The checks are tied to the observed GitHub Actions app
  ID 15368, rather than an arbitrary provider with the same check name.
- An up-to-date branch and resolved review conversations.
- Linear history. Squash and rebase merges are enabled; merge commits are disabled.
- No force pushes and no branch deletion.

Zero independent approving reviews are mandatory. This is a deliberate workflow
choice: the agent can prepare a PR, satisfy CI and merge without repeatedly waiting
for the sole owner to review their own work. Actual requested changes/conversations
still need resolution. Stale approvals are dismissed, and any future human review
remains welcome. Code-owner approvals, signed commits and deployment approvals are
not imposed by this policy.

Auto-merge and deletion of merged feature branches are enabled. The normal workflow
is to create a feature branch, commit/push it, open a PR, wait for the required checks,
resolve feedback, then squash/rebase merge. If `main` changes, update the PR branch
and rerun checks. Do not bypass or temporarily disable protection to publish progress.

The required job names are part of the protection contract. Update protection when
renaming jobs; otherwise GitHub may wait for a check that no longer runs. CI still
qualifies only its documented synthetic/Linux and Windows cross-build scope, not
the complete professional DAW or native Windows runtime.

See [verified settings](../tests/results/repository/2026-10-06-main-branch-protection.json).
