# Retained original Windows publication refusals

Source `0b68766bdf0259f9ec2bce812dd4d7eb406a5930`, tested merge
`e9afdf6bbc5ae44124ac429131215a9d07c63b57`, run 38097129636.
All four development jobs passed. The native unit log records 90 assertions:
actual owned-parent ERROR_ACCESS_DENIED, exact original DACL restoration and
retry, and actual primitive sharing refusal followed by release/retry.
See [scope](../../../../docs/167-windows-publication-refusals.md).

`capture.zip` includes only original text/JSON: four complete job logs, the
native CTest log, artifact receipt, scope qualification and 310 frozen Git input
bindings. The original 34 MB CI artifact stays local; only its CTest log was
extracted. No executable, synthetic WAV, installer or credential is included.

Run `python3 tests/results/M2/2026-10-11-windows-publication-refusals/verify.py`
in a full clone to audit exact members, source path set/blobs, tested merge
parents/tree, checkout identities and native refusal events. Historical source
and tested merge are retained on source-backup branches. No code/audio replay.

This source predates the later publication-root validation correction. These
logs qualify the original refusals; they do not qualify an updated head,
installed publication, writer interruption, full disk, power loss or processing
quality. Other test successes in the retained complete log have their own
contracts and are not expanded into parity claims here.
