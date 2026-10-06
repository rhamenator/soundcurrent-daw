# Windows development without a purchased signing certificate

Date:2026-10-06. Owner constraint: purchasing a certificate is currently
unaffordable. Windows functional parity remains required.

## Equalizer review

The initial read-only review found both equalizers' then **uncommitted** packaging changes
default to the existing signed primary VB-CABLE, retaining their own driver for
later signing. Ordinary app/installer artifacts remain explicitly unsigned.
Driver requirements are deferred, not represented as qualified. The
[review receipt](../reuse/reviews/2026-10-06/windows-signing-budget-review.json)
records exact working-file hashes plus HEADs; these are not committed upstream
release claims. All24 already borrowed DSP/profile/editor inputs still match.
No equalizer repository or working file was changed.

The subsequent [committed-source review](../reuse/reviews/2026-10-06/windows-signing-budget-committed-review.json)
observes public EQ `28e6f4779bbe0ed48b2e825aa5370f76670b64df` and Studio EQ
`70b0d14264c48bf229992f11495fa6e31b5b9180`, both clean at observation. The five
policy/build/setup files in each repository are now committed. Only the native
driver bookmark changed its bytes since the initial policy receipt, adding its
preserved Git bookmark. The new commit range also contains Windows routing,
packaging, native driver preservation and preview documentation changes; these
platform components have not been imported or independently qualified for the
DAW. All24 registered DSP/profile/editor files equal both the new committed
HEADs and existing retained snapshots. Original adaptation provenance remains
unchanged. Equalizer preview publication recorded upstream does not authorize
publishing this DAW repository.

## DAW decision

Continue the existing user-mode WASAPI endpoint adapter plan. Microsoft's
[WASAPI description](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
describes application streams through the existing audio engine/device stack.
Engineering inference: ordinary DAW capture/playback does not need a new
SoundCurrent kernel driver, APO, or virtual cable. An existing cable may be
selected as an endpoint if explicitly requested and installed separately;
do not add it as an installer prerequisite or silently change system defaults.

Local build, offline rendering, user-mode audio and Qt workflow qualification
must not require a purchased signing certificate. Keep local artifacts clearly
unsigned with exact source/build/hash receipts. Native Windows execution,
device/runtime/installer qualification remain open; this decision supplies no
new Windows pass evidence. There is no new driver/installer/script dependency.

## Distribution choices remain separate

Unsigned artifacts can trigger warnings or policy blocks. A hash receipt is
integrity evidence, not a trusted publisher signature. Preserve Windows trust
controls and report the actual blocked workflow; no blanket security-policy
changes belong in setup. Microsoft describes these distinctions in
[SmartScreen reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

[Microsoft's signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
describe free Store signing for certified **MSIX** submissions and qualifying
open-source signing via SignPath Foundation. Neither is approved or configured
here. Store MSI/EXE submissions have different publisher-signing requirements.
Do not infer account eligibility, GPL/source-delivery compatibility, plugin and
device access, or store certification from this documentation. Keep direct
distribution, sandbox/package behavior, updates, install/uninstall and release
trust evidence separate from functional and processing-quality evidence.

No subscription, certificate purchase, account enrollment, external application,
publication or equalizer-code import was performed. Before a public Windows
release, choose and qualify an affordable distribution/trust route with the
owner. A signing expense must not stop independent product development, and
unsigned development is not a claim of a qualified public installer.
