# Preserved first qualification failure

Source d9c7a47. Three suites passed; the source/clock fixture incorrectly expected
all initial assemblies to bake 1.5x speed. The 48 kHz fixture is a physical-domain
assembly and retains its playback speed separately. Commit e50e85f corrects that
expectation using the stored assembly domain, and checks dominant pitch as well
as the requested fundamental. The subsequent qualification uses a fresh corpus.

The complete original generated projects remain in
`.cache/comp-pair-recovery/build/comp-regeneration-source-evidence/run-wjyankq7`.
No producer deadline or quality guard was relaxed.
