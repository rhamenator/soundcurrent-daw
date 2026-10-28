# Inactive track-version render recovery

The retained stretch-render inventory now checks inactive audio arrangements when
classifying a verified operation as already attached. Switching to an empty
version must not mislabel a retained render as stale. Classification still occurs
after manifest, source binding, intent, and rendered-media verification; an
inactive reference cannot bypass those checks. Already attached operations do not
produce a new adoptable result.

The focused controller regression creates a rendered arrangement, switches to an
empty Unicode-named version, saves and reopens, checks attachment classification,
checks missing-media refusal and byte-preserving restoration, removes the inactive
version, and restores it through Undo. Removal retains the media on disk but no
longer counts the unreferenced render as attached. Original manifest bytes remain
unchanged. This does not implement automatic relinking or garbage collection.

## Qualification

[CI39084895518](https://github.com/rhamenator/soundcurrent-daw/actions/runs/39084895518)
passed all 15 jobs at `4cd19184248bacbea328f507d4a6b4730bfc99c5`.
The focused `desktop-stretch-controller` test passed on Linux (23.51 seconds)
and Windows (15.91 seconds). All 15 original job logs and 11 available artifact
ZIPs were retained locally, with artifact size, SHA-256 and ZIP CRC verification.
The [evidence index](../tests/results/M2/2026-10-18-track-version-recovery/ci.json)
records exact identities and focused result lines; it does not embed the large
original archives or claim native replay from reading retained evidence.

PR195 was squash-merged into `codex/windows-comp-preview-delivery` as
`39645c8c6127ff91d9dfc362d0e1210872ba653e`; the tested and merged trees were
verified identical. This was parent-branch integration, not integration into main.

No installed preview has changed. The separate Windows preview candidate remains
pinned to `9917e8c9d79b39c7f2f350deb924a8adf80c8e1a`. Its native setup,
basic helpers and stretch-v4 tests passed, but the protected-warp comparison
`attack-bed/nonuniform` exceeded its original 10-second observation deadline.
The observer reported 64.01 seconds, exit 1 and no retirement pending. Host I/O
pressure rose during that session; causation is unconfirmed. The failure archive
is retained locally (71,994 bytes, SHA-256
`2bb7e457c4ff531357c8db61aa23dcc124b0e3fdfeda697d18e31ab22d60dfa1`).
No deadline was relaxed. Main-window and installed-preview qualification remain
pending. This failure concerns the older preview candidate, not the passing
PR195 CI revision.

Next: run the prepared, hash-bound comparison diagnostic during a brief,
low-contention Windows session. A focused diagnostic pass alone will not qualify
the full protected-warp suite. Full recovery and track-version parity remain open.
