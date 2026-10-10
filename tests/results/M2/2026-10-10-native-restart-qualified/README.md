# Current native Windows qualification and local installation

Source `4fb8c1cdf1b1365e5834256f4b39b51566dbed52`, tree
`d1b6be0bd9114af21d73a5dd6ba8172b53510c44` ; 2026-10-10 UTC.
See [checkpoint 157](../../../../docs/157-native-windows-restart-preview.md).

- `capture.zip`: selected original native records/owned synthetic audio, original
  SDK-free capture ZIPs, host/operator logs and scripts, package receipt and two
  screenshots. No installed/deployment binaries, installer, dependency archives,
  credential files or private capture configuration.
- `capture-members.json`: exact size/SHA-256 of every selected original member.
  Capture path separators are preserved inside original guest ZIPs.
- `summary.json`: derived bounded claims and hashes of local-only artifacts.
- `final-vm-state.json`: all VMs off; owned acceptance CD ejected.
- `verify.py`: checks retained bytes/identities/outcomes without replaying Windows,
  processing audio or accessing the local product/source archives.
- `capture_safety.py` / `capture_safety_tests.py`: bounded recursive refusal of
  Windows case variants, forbidden metadata names, renamed binary signatures,
  path escapes and oversized members. Checks preserve original captured bytes.

Run from any directory:

```sh
python3 /path/to/soundcurrent-daw/tests/results/M2/2026-10-10-native-restart-qualified/verify.py
```

The V4/V5 banks and six native offscreen groups pass. Staged main PID 912 and
installed main PID 5076 close normally with exit 0; setup PID 260 verifies 64 files.
The source archive separately verifies all 2,235 tracked blobs/modes. The installed
app opens a Unicode-named project; raw media remains unchanged. Original staging,
parser, unsupported UI Automation pattern and observation-deadline failures are
retained. The clone/runtime is not pristine. Installed render/restart/review and
physical audio are not qualified; the full goal is not complete.
