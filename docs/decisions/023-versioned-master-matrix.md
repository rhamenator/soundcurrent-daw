# ADR 023: persist a dedicated master without guessing old routing

Date: 2026-10-06. Status: accepted for M2c3.

Move plain `MixPlan` values into the session model and add an optional stable-ID
master carrying that plan and its own endpoint intent. Version 1.3 serializes it
strictly; older schemas migrate without it. Reuse existing preparation, routing
matching, semantic history, accepted-prefix persistence and native/offline graph
contracts. No new dependency, license or equalizer checkout change is needed.

Keeping master output on an inspected track would conflate track and project
routing. Automatically guessing old hardware or mixed-layout maps would change
sound during migration. A full bus/DAG compiler is still a required later
milestone; this sparse master state provides the versioned foundation now.
Track removal and its master detach are atomic/undoable. Matrix layout changes
require preparation; device assignments remain passive. The initial desktop
editor has a visible 4,096-entry bound with larger state preserved, pending
virtualization/load qualification. See [contract](../33-master-matrix.md).
