# Native monitor startup readiness consumption

2026-10-11. M2 progress; the full frozen functional, processing-quality, content
and native-project scope remains open, including M2–M11, imports, equipment
profiles, Windows parity, installation and European localization.

## Protected preceding diagnostics

PR140 run38161752733 passed all four required jobs. Native MSVC passed70 suites
in166.67s, including the startup suite and three original20-take/five-second
completions. Exact head0d50cbccd1a93a881324889b9af7f5eafdd6ce70, protected
base31db463ba36b568ce83e52a37476f228d9ca3afd, tested merge
597b418bb993883c57b344ba48625d640bc16810, tree
f3c30e27321a583c3b315495e263e41a2932d2fa and53 reviewed blobs were qualified
against original full job logs, current required checks and enforced protection.
An ordinary matching-head squash produced MAIN
7d800f210258d77c82b3f43fa904c6e1a29d9a6c with the same tree and exact base
parent; API and remote refs were verified. The [protected receipt](../tests/results/M2/2026-10-11-native-take-diagnostics-protected-main/summary.json)
retains those bindings and full original logs without executables or media.

The original failed attempt remains in [Checkpoint205](205-native-punch-out-diagnostics.md).
Later success is bounded non-reproduction, not an identified causal correction.
The original Windows failure roots remain unresolved.

## Prepared context and stable binding

The render context now consumes the immutable startup handoff through the optional
SDK pre-lease hook. Declaration and binding must agree on finite warmup limits;
actual native descriptor, generation, identities, output rate/channels/frequency,
capacity and maximum fill must agree with the handoff before ownership transfer.
The processing constructor independently checks the same handoff against its source
rate and capture configuration. Resource charges include fixed callback state.
Legacy callbacks retain a null hook and their original behavior.

Every attempt publishes the original SDK render clock exactly once, before a
warmup lease or PCM consumption. Zero/missing SDK timestamps remain zero/missing;
the independent actual QPC bounds waiting and is never substituted into a fit.
Pending startup requests native-only silence. Immutable readiness requires actual
processing publication, a full native-capacity FIFO, matching admitted geometry
and no prior delivery/discard. A ready receipt acquired after cancellation/fault
is diagnostic only. Content authorizes one exact original-clock/frame request;
direct fill, changed metadata and duplicate authorization are refused before PCM.
Once content starts, a live underflow is a fault rather than renewed warmup.

SDK lifecycle owns actual warmup acquisition/release/Start, frame/wait admission,
queue drain and Stop. Context checks its original end against decisions and stores
distinct warmup, callback/content, finite-end/slack and readiness observations.
Budget stops, owner loss, acquisition/release/Start failures and failed Stop retain
their causes; no readiness decision invents a successful SDK lease or end.
Cancellation is atomic intent; external owners and handoffs outlive all joins.

## Composed acceptance and corrected feasibility assumptions

The new composition uses real prepared capture, original clock queues, processing,
startup/output handoffs, stable binding, and portable SDK lifecycle with simulated
SDK calls. Mono/stereo44.1/48/96k sources use unseeded original clocks; the first SDK
timestamp is missing. Raw sample bits/stamps and headroom are retained. Full4097-
frame native capacity is available before the first content callback. Independent
conversion verifies the entire original generated prefix with maximum error0;
only the certified finite final suffix is zero, with native canaries intact.

Initial development fixtures refused finite finish while a real accepted monitor
packet was still queued, then refused an attempted truncation to the first ready
receipt's publication extent. Corrected fixtures drain the already accepted
packet and use the actual generated finite end; they preserve the immutable first
receipt and all already generated PCM. Original failed development logs are
retained. These were fixture assumptions, not a processing relaxation.

Three concurrent owners publish capture/render readings and pump processing.
Native consumption races actual ready publication, compares the complete pulled
prefix independently, and retains an interrupted Stop instead of fabricating
finite completion. Its wall-clock experiment bound is separate from unchanged
logical native limits. Barriers and allocations remain outside audited methods.
The Linux audit includes C/C++ allocation/free and pthread mutex interposition;
it does not prove coverage of every possible syscall or dependency path.

Failure workflows cover input ending before ready, cancellation before/after
ready, publisher loss/fault, original clock queue pressure before PCM, protocol
mismatch, live underflow, actual simulated warmup GetBuffer/Release/Start failures,
frame/wait refusal, missing SDK end and failed Stop. Resource rollback and exact
binding mismatches are checked. Schema1.0 refuses all five new context fields
independently, plus its preceding ten unsupported SDK observations, preserving
pending transactions and legacy state rather than silently dropping evidence.

## Frozen-source qualification

Sourcee5c7a468b790b7440bfb998ef48d7d3e6bd21d31, tree21243860405661e513b20cc3597daa629f9bd950.

Nine selected suites pass in each profile:

| Profile | Suites | Time | Composition checks | Maximum PCM error | Audited allocation/free/lock |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release | 9 | 1.63s | 1,979,916 | 0 | 0 |
| ASan/UBSan | 9 | 15.24s | 1,979,916 | 0 | 0 |
| TSan | 9 | 25.97s | 1,979,916 | 0 | 0 |

The [qualification packet](../tests/results/M2/2026-10-11-native-ready-consumer/summary.json)
binds634 exact Git/checkout inputs,27 Linux executables, six Linux objects and
five Windows compile objects, exact configurations/commands and full original
logs. Original and compressed hashes are retained; no executables/objects/media
are included. The preceding ordinary MAIN merge preserved the complete original
committed candidate tree; all conflict stages and the exact resolution are retained.
The subsequent audit-only change adds Linux libc/mutex interposition to the new
suite. Existing warnings from unchanged mixing sources are retained with original
compiler diagnostics, exact protected-MAIN blobs and system-header hashes; no
warning flags were suppressed and they are not classified as false positives.

Windows SDK adapter and four test targets compile/link. The five affected context,
stream, native persistence and two fixture objects compile without warnings.
No Windows runtime or hardware claim is made. Builds were sequential, single-job,
low priority, in isolated directories preserving preceding qualified binaries.

## Remaining gate and next implementation task

Native Windows execution of the new composition still needs fresh CI; MinGW
compilation alone does not qualify execution. No physical endpoint, VM, driver,
installed audio, latency, alignment, EQmix or UI gate is passed by these fixtures.
The native monitoring factory remains disabled.

Next implement auxiliary native-state1.1 encode/decode and explicit1.0 migration
for the complete original readiness/warmup dispositions, immutable tuple, raw hash
and inactive exclusive lease evidence. Verify success/fault/cancel/owner-loss
round trips, malformed/unsupported state rejection, old-state defaults, interrupted
transactions and actual recovery before composing the factory or native activation.
