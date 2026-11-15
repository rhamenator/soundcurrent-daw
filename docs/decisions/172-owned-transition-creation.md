# ADR 172 — owned transition creation from the timeline

Status: accepted for the experimental processed-handle workflow, 2026-10-14.

## Decision

The crossfade dialog offers an explicit **Save and render** action. It prepares
new jobs, registers both request references atomically against the current
project, saves the project, renders missing source spans, and assembles and
verifies the result. Applying audio remains a separate explicit action through
ADR 171. Ordinary source-handle edits remain available.

`PreparedCompTransition` is privately constructed and move-only. Preparation
reserves staging memory before session copies, hashes the trusted installed
helper and current executable, uses fresh operation/output IDs, preflights the
model, and writes immutable pair/assembly request sidecars on the assembly
worker. It grants no authority to edit another project or memory owner.

The canonical project controller checks ledger, project, root, opening epoch,
expected source geometry/processing and conflicting command payloads. It adds
the pair and assembly references in one history transaction. The GUI waits for
that command's receipt before requesting Save, then for durable saved revision
before starting the paired worker controller. Child processes are serialized,
supervised and reaped by the existing controller. Assembly runs afterward on
its disk worker. GUI and real-time callbacks perform none of that file/PCM work.

## Cancellation and recovery

Cancellation before registration leaves canonical state unchanged. Accepted
registration/save operations may finish; the progress UI explains that retained
jobs and project edits can remain. Cancellation does not delete owned recovery
files. Replacing/reopening a project invalidates the old operation. A failure
to save prevents child launch. Closing the application waits asynchronously for
both controllers, including canceled preparation, to retire.

Results are experimental. Cancellation is cooperative and is not a hard bound
on blocked filesystem operations. Existing-media-only transitions use the
ordinary handle edit; unsupported processing is refused by existing planners.
Repeated regeneration from previously assembled assets and nonuniform/protected
processing still need separate source-aware implementation and quality evidence.

## Deployment

Linux installation and Windows bundle collection include `sc-comp-span-worker`
next to the application. Packaging requires matching qualified helper identity;
old main-workflow receipts that omit it are insufficient for a new installer.
Existing historical bundle validation remains available through optional
helper requirements. No installer or installed/native audio claim follows from
source compilation or an offscreen Qt test.
