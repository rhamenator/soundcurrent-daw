# Security policy

## Reporting a vulnerability

Use this repository's **Security → Report a vulnerability** feature to send a
private report. Include the affected commit/version, platform, reproduction,
impact, and any proposed fix. Do not put credentials, private recordings, or
exploit details in a public issue.

If private reporting is unavailable, contact the repository owner through their
GitHub profile to arrange a private channel; keep the initial public message free
of sensitive details. Response and remediation times are not guaranteed.

## Supported development version

Security work currently targets `main`. There are no supported production release
lines or qualified installers yet. Historical development commits are not
maintained release branches.

Projects, media, equipment profiles, and future plugin/import formats cross trust
boundaries. Parser validation and recovery tests exist, but the full planned
plugin isolation and hostile-input qualification remain incomplete. See the
architecture and acceptance documentation for the current boundaries.
