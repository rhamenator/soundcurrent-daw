# Original completion returns and clock handoff state

Checkpoint231 continues the full active suite goal. Owned controller/Qt integration,
verified alignment, installed/native platform qualification and later parity remain open.

The render boundary retains the first valid final native buffer return in a separate
completion cell before release-publishing completion. Last callback and last audio-bearing
return remain original independent receipts, even when EOF arrives after the audio was
returned or extra silent callbacks occur before control joins. This adds fixed bounded
storage charged through sizeof admission; callback work stays bounded/allocation-free.

The existing inactive journal inspector streams only the verified common-prefix render
log. It retains the first allQueued/finalContent record with original generation/sequence,
and excludes uncommitted tails. Recorded terminal claims compare canonical completion
bytes against that verified original receipt. They no longer require the last callback
or last audio-bearing callback to be the completion receipt. Queued output still does
not prove audible drain or recording alignment.

New owner state schema1.1 persists completion and both original clock-handoff fault
causes/attempted readings/accepted-delivered-discarded counts. Attempted readings preserve
positions, POSIX100ns, frequency, reliability and discontinuity flags. Count validation
checks delivered+discarded <= accepted without overflow. Version1.0 readers retain exact
old field sets/conservative old terminal semantics and do not invent missing diagnostics.
Unknown versions and unknown fields still refuse.

Tests cover actual render late EOF/extra callbacks, journal completion and uncommitted
prefix exclusion, owner reopen/original clock counts, synthetic terminal ordering and
altered-native-return refusal, exact rejected clock reading widths/counts and version1.0
migration. Four actual process publication interruptions remain in the state tests.

Implementation a52c2f0 binds829 exact selected source/checkout inputs. Four current
Release suites pass2.49s. Genuine private PipeWire1.6.2 preserves99,072 raw
frames and delivers8,192 processed frames to the sink,387 capture/408 render
receipts, with exact durable reopen and zero audited callback allocate/free/lock
hits. Nine current Windows objects compile warning-free; current sanitizer and native
Windows runtime qualification remain pending. The initial build used stale generated
include paths after a CMake edit during that build, then failed to find the JSON
header in the new test. Original commands/log retained; regeneration built current
inputs successfully. Older mixer warnings remain visible. Earlier local clock rejection, PR164 owner rejection, PR163 pool
pressure and older timing failures remain unresolved; this receipt fix does not establish
their cause. PR165 originalrun38321227921 was live when this work began; retain its terminal
logs/artifacts before any subsequent CI correction. No VM/physical audio/equalizer changes.

Evidence: [bound summary](../tests/results/M2/2026-10-12-pipewire-completion-receipts/summary.json).

Next: qualify current sanitizer/native Windows results, retain original PR165 terminal
evidence, then integrate the owned route/state into native controller/Qt recording.
