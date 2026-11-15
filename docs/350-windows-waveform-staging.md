# Windows waveform qualification and deployment staging repair

Original CI run `38814953536`, desktop job `116697173990`, executes the preceding
source `79aa56d` on native Windows. All 26 selected desktop tests pass in 72.68
seconds, including the full-window waveform workflow (81 assertions), rendered
waveforms (8), cache (112,881), and controller lifecycle (106). This confirms the
exact-CRLF test-helper repair and the waveform drawing/retry workflows from
checkpoints 347/348 on that runner. It does not qualify checkpoint 349's channel
selector, an installed preview, physical audio, or professional performance.

The crossfade font markers place about 1.62 seconds in font creation, with glyph
coverage completing immediately afterward. Its full dialog workflow passes. This
is another bounded observation, not a causal explanation of the older untraced
timeout or a cold-start performance guarantee.

## Original packaging failure

The subsequent deployment-input collection fails before preparing an installer.
The complete original job log and original artifact `12009699688`
(`windows-preview-build-inputs`, 80,501,504 bytes) are retained locally; the
artifact's size, SHA-256 and ZIP CRCs were checked against GitHub's metadata.
Re-auditing its actual deployment directory identifies exactly:

```
missing=['sc-waveform-worker.exe']; unexpected=[]
```

The PowerShell copier maintained a separate six-application list. Checkpoint 347
added the waveform worker to the Python collector's required set but omitted the
copier. The strict audit correctly rejected that incomplete payload. The original
failure remains retained; no rerun replaces it.

## Repair and regression coverage

The PowerShell collector now invokes the Python staging command. Staging and
auditing use the same application list, including all seven application/worker
executables. Staging checks the entire source set, native x64 PE envelopes and
destination availability before copying, and refuses links or overwrites.
Final collection still verifies the actual native platform, source identity,
application hashes and pinned runtime dependencies. Staging alone grants no
qualification. File-set diagnostics now name missing and unexpected files.

The packaging test invokes the same staging CLI on synthetic PE envelopes and
compares every copied byte. It verifies refusal of a missing waveform worker
before any copying, preservation of an existing stage, and rejection of missing
or extra executables by the final audit. These are portable delivery-policy
tests; synthetic PE files are never executed. Both focused packaging CTests pass
in 1.82 seconds, including the existing 118 protected-worker package gates.

See the [evidence manifest](../tests/results/M2/2026-10-15-windows-waveform-staging/manifest.json)
for exact source, original native log, deployment inventory/audit, artifact hash,
and local regression results. The large original archive is retained locally;
the public evidence packet contains its metadata and verified inventory.

Native staging requalification and installed Linux/Windows preview refresh remain
next. Preserve all original terminal run logs and artifacts before advancing the
PR. Existing installed previews remain at `97e9ecd`; this checkpoint neither
publishes binaries nor establishes full DAW parity.
