# Positive controller workflows using installed production workers

The test-only `--installed-workers` mode of `sc-controller-restoration-tests`
accepts an explicit absolute component directory. It supplies that directory only
for the four production ControllerOptions program defaults. Existing explicit
fault/qualification program overrides remain intact. The ordinary controller
suite and production defaults remain unchanged.

The mode runs four existing positive cases: accepted asset restoration, saved
staging, saved publication, and manifest-only staging. Their existing operation,
PID, exact bytes, cleanup, history and accepted-owner assertions must pass through
installed copies. It emits a distinct completion marker so these subset counts
cannot be confused with the ordinary full controller suite's counts.

`installed-restoration-controller` installs the explicit qualification component
into a temporary path containing spaces, checks its exact entries, executable
bytes and Linux modes, runs this controller mode, and rechecks installed entries
and bytes afterward. Raw install/controller output, process identity, executable
and source hashes, and a success-only receipt are retained on Linux and native
Windows. The existing controller process budget remains 120 seconds; the new
outer 150-second CTest budget also covers a bounded 20-second installation.

The driver, wave checker, libraries and qualification dependencies use the build
environment. This is evidence for these four installed controller workflows,
not a standalone dependency bundle, default preview package, native installer
lifecycle, broad recovery parity or authorization to load a project through an
installed GUI. Manifest staging remains passive and grants no accepted Session,
root/history, publication or autosave authority. Existing preview payloads and
the ordinary controller test's 120-second timeout are unchanged.
