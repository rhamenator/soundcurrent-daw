# ADR 186: compile the desktop window once per configuration

Status: implementation under qualification.
Date: 2026-10-14.

Original Windows CI run 38664035143 passed all 26 desktop tests and retained its
artifacts, but its overall desktop job was cancelled at the twenty-minute budget.
Its full log shows eleven compilations of `ui/studio_window.cpp` between the
application and selected test executables. Source code is identical; the file's
conditional compilation uses only platform macros, not fixture-specific flags.

Build that implementation once in the static `sc-ui-window` target. Share its
existing controller/widget dependencies and `QT_NO_KEYWORDS` through public
usage requirements. Keep platform warnings, strict compilation and UTF-8 source
handling. Each executable still builds its own main/test code and any real-time
audit instrumentation, and keeps its original link options and test registration.
The application-only native backend flag remains on its main executable.

No source implementation, test selection, worker policy, job deadline or test
deadline changes. This also reduces repeated local compilation. Qualification
must inspect the generated build graph for one window compilation and execute
representative comp, editing, import, localization and recording-panel workflows.
Native CI still needs to measure the new complete job duration. Compilation
savings inferred from the original trace are not a measured new native timing.
