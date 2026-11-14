# ADR 180: Partition the complete Linux CTest inventory

Date: 2026-10-14. Status: implemented; fresh CI execution pending.

## Evidence

Original run 38650069106 built successfully but reached its 30-minute whole-job
deadline after 92 of 188 tests passed. The remaining tests did not execute.
Preserve the original terminal APIs, complete logs and artifacts. This is not a
passing suite or evidence that an individual test hung.

## Decision

Run two Linux matrix jobs with the existing build configuration and unchanged
30-minute limit. Run CTest sequentially inside each job. Discover the complete
inventory from CTest and assign each name exactly once using deterministic
greedy balancing. Prior original run durations are scheduling estimates only;
unknown tests receive a default estimate and are included automatically.

Reject duplicate inventories, empty partitions and invalid estimates. Ask CTest
to interpret each generated selection and require exact agreement with the
planned names before execution. Retain the actual inventory, both partitions,
selected command and exit code. All test deadlines and acceptance gates remain.

Keep the exact required check name `Linux desktop and synthetic tests` as an
aggregate job that runs even after failure and succeeds only when the matrix
succeeds. Leave branch protection unchanged. Each partition also runs the
existing retained-evidence verifiers; duplicated verifier work is acceptable.

The local partition test establishes coverage, not remote timing or completion.
Fresh CI must establish whether both jobs fit the deadline. A new test suite or
slower runner may require repartitioning based on actual retained evidence.
