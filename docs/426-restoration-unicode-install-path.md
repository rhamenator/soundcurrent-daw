# Unicode paths for installed restoration workers

The native component refusal and positive controller tests install their four
helpers beneath a directory with spaces, accented Latin, a decomposed accent,
Greek and Cyrillic characters. Both wrappers share the exact
`europe-unicode-v1` label, rather than relying on the host account name or locale
to supply a non-ASCII path.

Each successful receipt records the label, profile and actual temporary install
prefix. Raw install commands are retained; the refusal test also retains each
worker's actual command, and the positive test retains its controller command.
The existing native PID, exit, byte identity, operation, file-set, history and
mode checks still apply. Current native Linux and Windows execution and original
artifact audit are required before accepting this path qualification.

This exercises the installed helper location and working directory. It does not
establish coverage of every European script, Unicode normalization equivalence,
all project/media paths, translated interfaces, standalone runtime dependencies,
installer lifecycle or an installed GUI. Temporary prefixes are removed after
the tests, while their path observations and process evidence are retained.
