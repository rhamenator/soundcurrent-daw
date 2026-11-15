# ADR 175: Verify completed assemblies through their original selections

Date: 2026-10-14. Status: accepted for the bounded review foundation.

## Context

An adopted clip no longer matches the source clip that its original rendering job
selected. Its pending assembly reference has also been consumed. Reloading that
job against today's timeline rejects valid history. Treating an assembled WAV as
a new raw source instead loses original processing and timing provenance.

## Decision

Retain the canonical recipe's decoded source selection under its resource lease.
Add explicitly named snapshot loaders bound to an independently recorded pair
digest. Existing current-selection loaders still check the live selection; the
snapshot APIs grant no adoption authority.

The disk/control-side history reviewer reconstructs a temporary original session,
adds the stored pair and assembly references there, and invokes the existing
request/completion/complete-PCM verifier. It compares reconstructed playback
provenance to the stored adopted source. It never installs that temporary session
as the live project.

An iterative queue reviews each unique assembly once. The queue also includes
historical dependencies discovered in the original selection. Bounds on assembly
count and total output samples are admitted before each new verification. Existing
per-job file, memory and cooperative deadline bounds still apply. This does not
establish a hard total wall-clock deadline or an OS memory limit.

The review retains the original source owner plus deduplicated original assets,
histories, pair references, retained takes and track layouts. Original selections
may include an unchanged side with no generated output; that side's declarations
must still be captured. A later metadata-freshness check rejects edits to those
dependencies, while permitting unrelated project-title edits.

## Limits and follow-up

The review is a file-integrity and algorithm-consistency result at review time.
It is neither permanent filesystem authority nor a sound-quality certificate.
Future production must recheck original files and metadata before and after
rendering, then carry the complete dependency guard through queued adoption.

Existing version-two span/pair gates remain closed. Explicit output clocks,
accumulated pitch, repeated actual rendering, mixed unchanged/assembled-side
acceptance, and retained takes backed by assembled artifacts still need
integration and evidence. No callback, GUI, installed app or audio route changes
are required for this foundation.
