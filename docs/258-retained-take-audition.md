# Retained take audition

Checkpoint 258 adds transient audition of a selected retained take. This advances
P006 without replacing arrangement clips or publishing a project edit. Full
reference/native/installed/listening qualification remains separate.

## Workflow

Select a track and retained take, then choose **Prepare take audition**. Choose
outputs in the existing playback panel and press Play. Preparation alone activates
no endpoint. The transport identifies take audition. Stop and choose normal
Prepare playback to return to the arrangement.

The entire retained lane plays in its saved project positions, including gaps,
with its clip processing and the selected track's EQ. Audition ends at its last
clip; the arrangement loop does not make it repeat indefinitely. The same
prepared engine and output-routing checks serve ordinary playback and audition.

## Ownership and persistence

`makeTakeAudition` constructs a validated control-side Session copy. It preserves
retained clips/assets and assigns transient audible IDs with source-take provenance.
The playback controller prepares only the selected track. The canonical project,
history, saved file, playhead/loop and export snapshot remain separate. No implicit
save, media creation or mutation is performed by preparing or playing a take.

Preparation uses the accepted-command barrier and a project epoch/root/ID guard.
Temporary model copies are charged to the existing project resource owner. Stop,
close and cancellation use the existing priority retirement path. A changed take,
asset declaration or project invalidates the prepared audition. Live selected-track
EQ updates use stable parameter IDs, preserve the temporary clip identities and
follow the existing accepted/applied receipt distinction. This does not publish
preview clips into the canonical model. Explicit user EQ edits remain ordinary
project edits; audition itself is not an undo transaction.

Rendered-clip and retained-take audition have distinct source markers. A stretch
dialog cannot mistake a take preview for its own rendered result. Normal playback
preparation discards the audition source and restores the arrangement/loop intent.

## Acceptance scope

The controller corpus passes 93 checks, including transient source/provenance,
no activation during preparation, finite audition despite a project loop, live EQ
acknowledgement, changed-take/project refusal, Stop/shutdown, and return to ordinary
arrangement preparation. Its endpoint fixture qualifies controller ownership and
DSP event flow, not physical/native audio.

Actual original recordings distinguish five takes. Each transient take model
renders sample-exact owned PCM rather than the arrangement; saved project,
canonical arrangement export and all raw hashes stay unchanged. The combined
retained-take corpus passes 13,819 checks with callback audit zero. The Studio-window
button/barrier/output-selection/Play/Stop/return/stale-opening/close workflow passes
37 actual Qt checks;
current-source native Windows and installed/listening gates remain pending.

The initial status/Stop observations raced the GUI timer and now wait for visible
controls. The actual return failure exposed a temporary-model retention bug: Stop
now restores the retained canonical latest model if the slot still contains the
audition projection. Original failures and causal regression results are retained.

Localization uses real Qt Linguist extraction: 1,038 keys, 34 partial catalogs,
zero native-reviewed or fully UI-qualified languages. Catalog scaffolding is not reviewed
European language support. No VM, owner music interruption, physical endpoint,
equalizer edit, installed-preview replacement or binary release upload occurs here.
The frozen product baseline and full M2–M11/F/Q/C/N/X004/X005 goal stay active.
