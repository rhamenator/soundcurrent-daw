# Manifest-only parent dispatch and staged observations

The shared controller integration needs a trusted parent boundary for a saved
project that has no recovery journal. `SavedManifestDispatchTicket` binds before
launch to the original private source ticket, exact retained control bytes,
actual checker report/PID, exact native source-file stamp, explicit root, current cold owner context and a fresh
operation whose absence is checked under the original root stamp. It neither launches a process nor changes a project.

Prelaunch verification remains strict: a changed owner, original native root
stamp, selected source fingerprint, report, ledger or control frame refuses the
dispatch. The parent must retain its actual audit and approved source handles
through child launch, cancellation, termination and reap. Identifiers and public
completion fields do not replace that private preparation or the current owner.

After the parent has independently observed and reaped the actual worker, the
ticket can inspect passive stage evidence. It checks the original native and
named root identity, original pinned and named manifest/canonical facts, original
source bytes, exact planned record/current context/observed worker PID, staged
bytes and on-disk intent. Original file facts and root identity bracket the
inspection. An inspected audit must match the original ticket’s manifest hash and canonical presence/hash/extent, not only its cached directory stamp. The full root and operation-directory change stamps remain stable throughout; both stage files stay pinned while their names and contents are checked again. Cancellation and resource ownership remain explicit.

This is a separate observation boundary. A child legitimately adds a stage
folder, so the original directory change stamp no longer passes ordinary audit
verification. Inspection does not update that stamp, turn an exited record into
a live stage, authorize publication, accept a Session, adopt media or restart the
project. The narrow friend can recheck original files without exposing a general
root-stamp bypass. Normal audit/source verification still refuses afterward.

The supplied worker PID is matched to the record; the API cannot establish OS process origin or reaping from a number. The trusted supervisor must use its actual child and pipe, retain the original owners, reap that child and freeze other authorized writers. These read-only observations do not prove authorship against an unrelated writer that can forge an entire matching stage.

The actual isolated manifest-worker case exercises binding before spawn and
observation after reap from the same original native parent owners. Additional
checks reject changed owner flags/context, checker/worker PID, ledger, original
control, intent, staged bytes and canonical visibility. They check move/credit
retirement, cancellation and continued strict-audit refusal. Linux additionally
checks source mutation; Windows retains its native source write-exclusion pin.
No test deadline, validator or inventory is reduced.

The next integration step is the shared parent supervisor and serialized
ProjectController command consuming `SavedManifestPreparation` directly. This
change supplies its proof boundary; it does not yet expose that command or add
publication permission. Linux and native Windows execute the same C++ source.

The Linux in-place-manifest regression preserves the directory stamp, acquires a new audit/control for changed manifest bytes, and lets the real worker produce that stage. The original dispatch must reject the substitute audit even though its own pinned files match the changed project. Windows retains native write exclusion for the original pinned manifest.
