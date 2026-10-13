# Windows media dependency reproducibility experiment

Checkpoint 254, 2026-10-13. The Windows preview still requires an independently
reviewed libsndfile DLL. The existing dependency pin remains unchanged.

## Experiment

The desktop CI job builds the same hash-pinned libsndfile 1.2.2 source twice,
using fresh `sndfile-build` and `sndfile-repeat` directories in the same native
x64 MSVC environment. Existing directories cause refusal. Both builds use the
same Release shared-library configuration, without external codec libraries,
MPEG, experimental code, tools, examples or tests. App desktop acceptance tests
then use the first DLL.

The link options are `/Brepro /PDBALTPATH:sndfile.pdb`. Microsoft's
[/PDBALTPATH documentation](https://learn.microsoft.com/en-us/cpp/build/reference/pdbaltpath-use-alternate-pdb-path)
explains that the option changes the PDB path recorded inside the binary, allowing
a path independent of the build computer's directory layout. `/Brepro` is an
experimental choice here: actual linker acceptance and complete byte comparison
must establish whether this toolchain produces identical DLLs. Source pinning
alone is not binary reproducibility.

The review script retains both DLLs, byte counts, SHA-256 hashes, CMake caches and
Ninja build rules. Different outputs fail the build and retain the mismatch. An
`always()` artifact step preserves the comparison independently of later app
compilation or preview collection. Successful collection also includes the review
inside its source/deployment bundle.

## Qualification boundaries

Local PowerShell syntax and package input/refusal tests pass. Native execution is
pending for this change. Two equal outputs on one runner would establish limited
same-environment reproducibility, not cross-toolchain or cross-machine proof.

No dependency identity is automatically adopted. Before updating a product pin,
review the actual source/configuration, license and native media behavior, and
verify the DLL actually loaded by the app/helpers. Interactive main-window,
helper/worker and matching installer/source acceptance remain separate gates.
The full DAW scope and European localization work remain active.

## Preceding build retained

Original run [38444647729](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38444647729)
for source `ca965d701c8606ac8d758c7a18c2147e56ba3282` passed all four jobs.
All original job logs and artifact ZIPs are retained locally under
`.cache/windows-media-reproducibility/pr187-review/`, with downloaded sizes and
SHA-256 hashes checked against the artifact API. The preview archive is
123,088,897 bytes, SHA-256
`f67b1c6c8e419d24c98346d1c5ec8900db2bdb19402f316f0330af395818c7f4`.
No native installer, local VM, UI/catalog or equalizer changes were made here.
