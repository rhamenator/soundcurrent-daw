# Windows render and startup observations

2026-10-10 UTC. This records the existing native MSVC build at
`8b46e8975e832edc3fc8c19b1c62d3edf96277e2`, tree
`fe3197c30e20c30493f03eb9ba9720a12f7de2e2`. It follows the failed attempt retained
in [the previous checkpoint](../2026-10-10-installed-protected-preview/README.md).
The observations are not rebound to the newer repository head.

The independent `soundcurrent-win11-dev` clone was the only running VM, capped
at four CPUs and 6 GiB. No native rebuild, installer, original-VM maintenance,
physical audio activation or equalizer changes. Developer SDK DLLs remained on
PATH. All VMs were independently observed shut off after testing.

## Actual outcomes

- A single owned stereo 48 kHz/12,000-frame request completed in 0.243 seconds.
  The ready acknowledgment and complete protocol packets, actual PID/exit 0,
  executable hash, request and raw/derived float WAVs are retained. A 3/2 duration
  and +1200-cent pitch request produced 18,000 frames, with headroom preserved.
- The actual native v4 bank completed: 385 checks including source/executable
  checks, 53 completed jobs, and 1,045 shared adoption/live/export/reopen verifier
  checks. It produced a separate operation with byte-identical derivative WAV.
- The main application opened its real default-style window in console session
  1 and accepted normal close, exiting 0. This was a startup/close observation;
  no project workflow, installed deployment or audio endpoint was activated.
- The actual v5 bank timed out in its existing pipe reader; its cleanup reader
  also timed out. No completed v5 qualification report was written, and the old
  harness did not preserve the failing request or final exit. The log is retained.
  Its failure location is inside a loop and does not establish which bank item
  failed. No root cause is claimed.

The earlier startup/v4 failures did not recur in this rebooted session. That
observation does not establish that the underlying problem is fixed. V5,
installed protected workflows, broad processing quality and full-suite parity
remain unqualified.

## Retained verification

`manifest.json` pins 14 original observation files totaling 254,055 bytes.
`verify.py` checks all member hashes/limits, exact historical source and executable
bindings, separate worker identities, ready/completion packets, all owned PCM,
float headroom, the v4 receipt and real normal-window close. It enforces the
unqualified v5/installation/cause flags. It does not execute captures or audio.
No executable, DLL, SDK, installer, credential or full desktop screenshot is retained.

```sh
python3 tests/results/M2/2026-10-10-windows-bounded-observation/verify.py
```

The local Linux log records both real render banks and the six observer failure
cases passing after the harness change. The stale-worker log separately records
an older cached Linux v3 worker refusing the current v4 request; the improved
failure message retained its actual exit, protocol and stderr. It is not a current
worker qualification.
