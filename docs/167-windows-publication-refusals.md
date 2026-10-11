# Owned Windows publication refusal tests

This M2 step adds native refusal fixtures to `stretch-selection-publication`.
The native Windows unit CI job already builds and selects that test. Production
publication, DSP, file formats and public APIs are unchanged.

## Parent access-denied workflow

Start with a saved Unicode project, raw WAV, existing selection and publication
lock. Retain the owned selection directory's original DACL, add a current-user
deny of FILE_ADD_FILE with no inheritance, and confirm an actual CreateFileW
probe returns ERROR_ACCESS_DENIED. Run the production `persistStretchSelection`
under the same denial: require explicit I/O refusal, no new leaf/temporary,
unchanged older files, resource baseline and available publication lease.

Restore the original DACL and query it again to compare the ACL bytes. Retry the
same operation, independently hash its final manifest and inspect it as
Incomplete because no helper render exists. Remove only that fixture's new leaf.
No user SID is logged and no privileges are enabled. RAII retains cleanup on
exception; explicit restoration must succeed before normal fixture removal.

FILE_ADD_FILE controls directory file creation. The ACL setter requires object
ownership or WRITE_DAC; these tests modify only their newly created directory.
The test target uses the Windows system Advapi32 library, not a new bundled
product dependency. [File access rights](https://learn.microsoft.com/en-us/windows/win32/fileio/file-access-rights-constants),
[ACL entries](https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setentriesinaclw),
[ACL setter](https://learn.microsoft.com/en-us/windows/win32/api/aclapi/nf-aclapi-setnamedsecurityinfow).

## Native primitive sharing workflow

Create/write/flush a separate owned temporary payload. Hold a native read handle
with FILE_SHARE_READ and no FILE_SHARE_DELETE, call the actual `publishMedia`
primitive, and require I/O refusal with original temporary bytes and no final
file. Close that handle and retry: require correct final bytes and no temporary.
This qualifies the primitive's sharing refusal; it is not a timed parent rename
hook. Windows sharing permissions remain in effect until the handle closes;
delete sharing includes rename access. [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

Warm the security APIs on the owned fixture before measuring the stable process
handle baseline; after both workflows, require the same count. Existing Session,
raw-audio, manifest, resource and lease checks remain enabled throughout.

## Current evidence and gaps

Initial source `8527f2e` compiles the Windows test translation unit with MinGW
C++20, warnings enabled and no diagnostics. The unchanged Linux fault branch
passes351 assertions in the same source. This is compile/Linux evidence only;
native MSVC execution and the two actual Windows refusal workflows remain
pending current-head CI. No Windows runtime qualification is inferred from
cross-compilation. No VM or audio endpoint is used.

The inherited recovery-dialog lifecycle correction has separate deterministic
red/green UI evidence; original dependent Windows failure remains preserved.
Linux publication-fault evidence stays frozen at its original a84e09b source.
Installed Windows publication, writer process interruption, full filesystem,
flush failure, power loss and processing quality remain separate requirements.
Full M2–M11/X004/X005, Europe-wide language qualification and F/Q/C/N remain open.
