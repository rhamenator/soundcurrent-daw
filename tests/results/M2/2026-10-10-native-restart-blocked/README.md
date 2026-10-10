# Native Windows restart qualification gaps

See [checkpoint156](../../../../docs/156-native-windows-restart-gaps.md) for the
scope, source identity, successful checks, failed gates and next task.

`summary.json` binds the retained archive and final VM state by whole-file
SHA-256. `capture.zip` contains the original native evidence ZIP, host logs,
active input scopes and local harness logs. The native archive retains actual
helper/desktop/build records, diagnostic owned WAVs and original refusals.
Its original whole-file SHA-256 is
`79fff9281fc93c29d587b736ff0c241c71813734f979c66cd19c8460b5a4fa23`.
Windows ZIP entry names retain their original backslashes.

This evidence deliberately contains failed runs. There is no complete native
protected-bank receipt, normal-window acceptance, installed restart acceptance
or new installer. Native records identify historical tested source `f1f4dbc`;
they must not be rebound to the later observation-harness changes.
