# Controller consumption of saved-source preparation

StageSavedRestoration accepts a current private preparation job and a strictly
increasing staging request. It requires the same empty controller and original
source-preparation epoch/revision. Paths, source overrides, inspection DTOs and
accepted-model replacement inputs are refused. Successful I/O admission consumes
the private preparation; failures after that point require fresh preparation.
Rejected commands preserve the previous private preparation and completion.

The serialized I/O worker revalidates the ORIGINAL private source ticket against
a fresh native saved audit, the actual completed/reaped source checker and a
freshly approved explicit source handle. It exports the original control proof,
never substitutes a newer fingerprint, and retains the original approved native
destination identity through child retirement. Its independently observed stamp
is compared with the ORIGINAL private ticket before spawn; a pathname reopen
cannot silently select a newer root. A new operation and staging job
are independent of historical intent fields. No accepted Session is fabricated.

The real shared saved-staging child receives an exact bounded request only after
its PID is independently observed by QProcess. The parent closes stdin, drains
bounded stdout/stderr, checks cancellation/close/deadline on its I/O thread, and
kills/reaps the actual child on every failure path. A joined watchdog bridges
cancellation, closing and the deadline into the stop token passed to every native
audit, proof revalidation, report copy, selected-source hash and post-stage read.
The source hash also checks the policy before each chunk. A full hash pass is not
an uninterruptible unit; only an individual native syscall remains cooperative.
Audit pins, approved selected source and native destination, original control and
checker bytes live outside exception scopes. They remain held through kill and
actual reap even when cancellation, expiry or malformed output throws. The
watchdog joins before those owners are explicitly reset; their admitted credit
outlives the owners. Native qualification probes check parent pins before kill,
after reap, and their release afterwards. Admission reserves the
512 MiB child policy plus framing/parser credit in the parent's same ledger and
scope. Native filesystem I/O remains cooperative around OS calls; this admission
is not a process RSS sandbox or a guarantee about an uninterruptible OS call.
The trusted deadline policy adds 1,000 milliseconds per started MiB of checked
source to the 20,000 millisecond base, saturating safely at the configured
30-minute maximum. A 1 GiB selection therefore has 17 minutes 24 seconds for all
parent/child work rather than the small-file base alone. This is a bounded policy,
not a claim that every storage device or maximum-size file has been qualified.
Qualification can disable the size allowance and use an independently controlled
clock to exercise expiry without requiring a fast host to reach a spawn seam.

A maximum-width native PID is used to preflight the complete request extent
before spawn. The actual observed bounded PID then replaces that placeholder.
The operation observation is populated only after an actual child starts; a
pre-spawn refusal has no operation, child or partial-staging observation.
Only a normal zero-exit, reaped child with one strict bounded planned-record
response can qualify. The parent compares the record and persisted intent with
its independent original asset, qualified source bytes, source/preparation/
staging context, new operation and actual PID. It independently hashes staged
bytes through its retained approved destination after the child's exclusive
writer handles retire. These are historical observations, not reconstructed
native owners, full post-stage original-root transition proof or publication
permission. Original audit/fingerprint checks and pre-create guards still run
inside the shared native child.

Once a child actually starts, staging-may-exist stays conservative even before
the request is sent. A missing, malformed or truncated reply, overflow, cancellation,
deadline, crash or closed pipe must never prove no files were created. The parent
retains honest PID/exit/reap/request facts and partial files for inspection.
Reported QProcess normal/crash status and a separate kill-request flag are
retained; a request does not attribute an exit that races it. Launch-attempt and
launch-retirement facts are separate from a confirmed started PID, so cancellation
while QProcess is still Starting never means that no launch was attempted.
exitCode alone, even zero, never establishes normal completion.
Refusals before launch have no operation or child identity. Failed or interrupted
launch attempts report their retirement without inventing a confirmed child PID
or exit. An auxiliary qualification-probe failure is recorded separately and
preserves the first Canceled/ResourceLimit/etc. cause while invalidating positive
verification. The saved record uses its own saved-schema extent limit explicitly.
The logical deadline requests cancellation/kill; it cannot preempt an individual
kernel call or promise a finite kernel retirement time. The I/O owner and all
native pins remain held until actual QProcess retirement, even if the kernel
keeps close pending. The GUI's asynchronous closing state remains honest; the
parent never detaches or declares a live child reaped to meet a timer. It
does not erase staging, publish canonical media, write publication receipts,
release accepted-model reconciliation, adopt a project or bypass strict Open.

Test workers are built only under BUILD_TESTING. Their compile-time-only modes
exercise actual mid-write retirement, stdout/stderr overflow and unsuccessful or
empty/diagnostic zero exits; none can activate in the production worker. A late
cancel/close after all I/O verification is checked again by the serialized control
completion and cannot publish positive staging verification.

The command and separate completion/refusal snapshots are internal controller
foundations, not new GUI controls. Single-use live publication permission,
canonical verification, strict Open/adoption, localized recovery interaction and
native installer qualification remain separate required work. Linux and Windows
use this same implementation and require actual exact-head native verification.
Historical preview deliveries, frozen product references and all processing,
content, import, equipment-profile, all-Europe localization and GPL/release gates
remain in force.
