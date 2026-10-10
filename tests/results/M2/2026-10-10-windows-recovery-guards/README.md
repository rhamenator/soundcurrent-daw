# Installed Windows recovery guards

Native source `4fb8c1cdf1b1365e5834256f4b39b51566dbed52`, tree
`d1b6be0bd9114af21d73a5dd6ba8172b53510c44`; installed sequence `20261010183111`.
See [checkpoint 159](../../../../docs/159-windows-recovery-guards.md).

- `capture.zip`: six exact original members, including both original guest
  captures, both executed operators, the original encoding audit and final state.
- `capture-members.json`: every selected member's size/SHA-256.
- `summary.json`: qualified control/guard workflows and unqualified playback,
  physical, interruption, power-loss, scalability and full-scope boundaries.
- `verify.py`: retained process/installed-module identities, four actual normal
  exits, Ready Prepare/Stop/Apply barrier, stale and altered-manifest refusal,
  exact before/after file hashes, original/corrected UTF-8 fixture differences
  and absence of output selection/Play. Uses the earlier bounded archive guard;
  executes no archived operator, native app or audio.

Run from any directory:

```sh
python3 /path/to/soundcurrent-daw/tests/results/M2/2026-10-10-windows-recovery-guards/verify.py
```

PIDs 8452, 9012, 2832 and 2120 each exited normally with code 0. The original
manifest alteration also misdecoded UTF-8 paths through an ANSI PowerShell read;
its refusal is retained without a ratio-only claim. A new copied fixture preserves
all original fields except ratios 499999/333333, retains the original binding and
is independently refused. No proven project or fixture file was modified by an
app process. No endpoint was selected and no Play/new render was requested.

This verifies control-side audition preparation/retirement and recovery guards.
It does not qualify audible/native-packet/physical playback, abrupt parent death,
power loss, scalable recovery, complete processing quality or full DAW parity.
Products and private transport/credentials are excluded. All VMs are off;
temporary transport is removed and both receivers' actual exit is 0.
