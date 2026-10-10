# Windows production-controller parent interruption

Original 2026-10-10 native MSVC/Qt Core development-clone observations.
See [checkpoint 161](../../../../docs/161-windows-parent-interruption.md).

The original native build is source46a6d3f; the executed corrected Python driver
is source8fc9db6. The original build receipt and both 613-input manifests prove
that only the driver changed. Probe/worker bytes stayed identical. Current
source copies and future merge/CI metadata do not relabel that compilation.

`capture.zip` contains 143 exact members: four final cases, original failed
lock/publication-read sessions, source/input manifests, native/host logs,
per-case terminal receipts and shutdown state. Its 113-MiB uncompressed contents
fit the existing 128-MiB recursive guard; no limit was raised. All programs in
the evidence are source text under this repository's GPL-3.0-only license.
No product EXE/DLL, installer, credential or private transport is included.

Run the retained-file audit:

```sh
python3 tests/results/M2/2026-10-10-windows-parent-interruption/verify.py
```

It does not execute archived scripts, replay native processes or audio. The live
regression is the Windows CTest `desktop-stretch-parent-interruption`.

Qualified: actual parent termination before ack/during partial/after completion;
live writer exclusion; incomplete intent/partial retention; explicit Ready review;
actual process identity/terminal linkage; unchanged raw/saved projects; numeric
unity float headroom. Initial operator failures remain separate failed evidence.

Unqualified: installed GUI interruption, SDK-free runtime, automatic parent-death
containment, exercised publication-retry branch, physical/native endpoint audio,
non-unity sound quality, large inventory, power loss and full DAW parity.
