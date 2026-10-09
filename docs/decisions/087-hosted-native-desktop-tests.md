# ADR087: hosted native Windows desktop workflow tests

Status: selected; first native execution exposed a test-fixture defect; corrected qualification pending.
Date: 2026-10-09 UTC.

## Context/options

The new import model/worker has MSVC evidence but its Qt workflow is currently
Linux-qualified. The owner requests brief local VM runs and avoidance of disk
contention; previous host crashes make sustained local VM builds undesirable.

1. Rebuild/test on a full independent local Windows clone: available after the
   successful owned disk comparison, but causes local disk/CPU contention and
   needs a bounded boot/build/session/shutdown period. Reserve for hardware,
   interactive or installer evidence that hosted offscreen tests cannot supply.
2. Use Wine or MinGW compilation as Windows UI evidence: neither establishes
   native MSVC/Qt/Win32 process/filesystem execution. Insufficient for this gate.
3. Add a hosted native MSVC/Qt job using the existing reviewed SDK: selected.
   Actual child-process/Qt/persistence/localization tests run independently of
   local VMs. No product dependency/version or license selection changes.

## Decision/consequences

Download the exact official QtBase6.12.0 archive and verify its size/SHA plus
runtime hashes/source anchor against independent product records. QtTest and the
offscreen plugin are test-only. Rebuild pinned libsndfile1.2.2 with external codecs
disabled. Use native MSVC, explicit runtime/plugin paths and existing real tests.
Keep current required branch checks. Add the new desktop context to protection
only after actual qualification, and retain failed as well as passing evidence.

Costs include Windows runner/download/build time, official archive availability,
compiler/OS drift and independent SDK identity review for future updates. Do not
select CI caches or another Qt module solely for faster passing runs. No SDK,
application executable or dependency DLL is uploaded. GPL/Qt/libsndfile source and
notice requirements for actual product installers remain separate.

Offscreen native tests do not replace interactive accessibility, installed main,
physical audio/driver/sustained workloads or full frozen-reference compatibility.
All those gates and broad project import/conversion remain required.

## First execution

At df20653 the native SDK/build succeeded and UI90/localization248 passed; the
controller's truncation fixture erased CRLF instead of JSON syntax. The retained
original artifact and controlled Linux reproduction support a test-only fix with
actual/LF/CRLF malformed cases. Local controller77 Release/sanitized checks pass;
corrected native qualification and required-context promotion remain pending.
