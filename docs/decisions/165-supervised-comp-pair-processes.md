# ADR 165: supervised paired comp processes

Status: accepted candidate-control service; main-window/adoption/quality open.

Use one off-GUI control thread and at most one child at a time, joining the
project owner's resource ledger. Require durable trusted references, explicit
application helper identity, full pre-spawn child reservation and observed reaping
before releasing it. Bind sampled live model/root/epoch/reference prefixes at
launch/readiness/start/final review. Final adoption needs its own canonical guard.

Retain bounded process logs and committed/incomplete jobs after cancel/failure.
Read-only recovery launches no child; render absent children and reuse verified
completions. Never silently resume or overwrite an interrupted/active job.
Preserve a committed first child when the second is canceled. Make shutdown a
priority request with asynchronously observable closure after reaping.

Process integrity and resource isolation do not establish sound quality,
curve/alignment/scope or physical/post-SRC adoption authority. Main-window wiring,
nonuniform/protected production, native/platform and quality gates remain required.
