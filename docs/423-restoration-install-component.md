# Restoration worker install qualification component

The explicit CMake component `restoration-qualification` installs exactly four
production worker executables: asset restoration, saved staging, saved live
publication, and saved manifest staging. It excludes qualification fixtures and
is excluded from the ordinary install. Existing preview payload inventories and
installer qualification remain unchanged.

`installed-restoration-refusal` installs this component into a temporary prefix
containing spaces, checks the exact file set and source executable bytes, checks
Linux executable modes, and launches each installed executable with an incomplete
request. Each real native process must refuse the request with its role-specific
protocol and actual launched PID, without staging or publication observations.
The four executable digests must differ, including the three variants compiled
from the same saved-worker source. Raw installation output, child output, exit
facts, source hashes, executable hashes, and parsed refusal observations are
retained under `installed-restoration-evidence` in Linux and native Windows CI.

The test uses the build's dependency search environment, including the original
executable directory on Windows. It does not qualify a standalone dependency
bundle. A refusal observation does not establish successful restoration or
operation/permit binding. Successful installed native workflows, trusted package
qualification inputs, default payload integration, GPL/source provenance, native
installer lifecycle and installed GUI authorization remain separate work. No
project root or publication permit is supplied by this test.
