# Installed Windows normal restart evidence

2026-10-10 UTC; installed native source `4fb8c1cdf1b1365e5834256f4b39b51566dbed52`,
tree `d1b6be0bd9114af21d73a5dd6ba8172b53510c44`, sequence `20261010183111`.
See [checkpoint 158](../../../../docs/158-windows-installed-normal-restart.md).

- `capture.zip` contains 18 selected immutable members: nine original guest
  captures, five operator scripts, two screenshots, the original render audit and
  final cleanup state. Original capture names/separators and unsuccessful receipts
  remain unchanged. No product binary, installer, SDK, credentials or private
  transport configuration is included.
- `capture-members.json` binds every selected original member's size/SHA-256.
- `summary.json` separates the qualified normal workflow from remaining gaps.
- `verify.py` checks retained process identities/exits, owned installed modules,
  helper hash, Ready/marker guards, saved Apply/Undo/Redo, Attached refusal,
  repeated WAV equality, all numerical PCM values, signed-zero boundaries, raw
  hashes and immutable selection/receipt state. It uses the earlier bounded
  recursive capture-safety guard and executes no archived script or product.

Run from any directory:

```sh
python3 /path/to/soundcurrent-daw/tests/results/M2/2026-10-10-windows-installed-normal-restart/verify.py
```

Main PIDs 3376, 6568 and 3644 each have actual normal exit 0. Helper PID 6584's
installed path/hash was observed; its modules and independent terminal state
remain unqualified. Saved Undo accounts explicitly for schema normalization
12 → 15. The protected core is bit exact; the complete protected span is
numerically equal with 128 signed-zero differences. Exports match each other
byte for byte and the derivative numerically, with 2,175 signed-zero differences.
Peak 1.5 linear remains intact. This is retained evidence verification, not a
Windows replay or full processing-quality test.

Audition, changed-target/altered-selection installed refusals, abrupt parent death,
power loss, scalability, physical audio, pristine runtime installation, full
processing quality and the full DAW scope remain unqualified. Product artifacts
remain local; all VMs are off and temporary transport is removed.
