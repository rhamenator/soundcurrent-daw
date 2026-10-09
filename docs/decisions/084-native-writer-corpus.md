# ADR084: Original native-writer corpus through public APIs

Date: 2026-10-09 UTC. Selected for X004 investigation; compatibility unqualified.

| Route | Functionality/platforms | License/maintenance/integration | Decision |
|---|---|---|---|
| Handwritten synthetic RPP only | Deterministic edge/failure fixtures on both platforms | Original GPL source; no source-suite dependency, but native defaults/quoting/serialization are not established | Retain for hostile inputs; insufficient as the native corpus |
| Public shared demo/third-party project downloads | Potentially broad source features | Per-project media/plugin/content redistribution rights and provenance often unknown; additional version/access costs | Do not use as a rights-cleared corpus without a separate audit |
| Unmodified writer with original scripts/media and public APIs | Native serialization and save/reopen observations, currently Linuxx86_64 writer7.82 | Proprietary tool with limited evaluation/licensing, kept private; original GPL scripts/fixtures; future writer/OS versions and API behavior need fresh qualification | Selected initial corpus; CI/product require no writer runtime |

Official [REAPER download/evaluation](https://www.reaper.fm/download.php) and
[ReaScript API reference](https://www.reaper.fm/sdk/reascript/reascripthelp.html)
support creating/saving/reopening owned projects and observing properties.
The downloaded runtime EULA was inspected privately. No proprietary binaries,
SDK implementation, algorithms, vendor bundled content or documentation are
copied into the DAW. A new authoring run requires a valid license/evaluation;
no purchase or continued-use entitlement is assumed.

Freeze exact runtime hashes and generated project/media/observation hashes;
pin7.82/Linux for this corpus independently of the unchanged Bitwig/Cubase
baseline. Newly generated GUIDs/timestamps legitimately differ. Preserve the
original corpus and qualify new authoring by recorded property comparisons.
Public code uses original scripts through supported APIs, not executable
inspection. Default factual project configuration is retained with authored
settings; dependency/data rights remain separately tracked.

Run the trusted writer in a bounded private namespace lab, without host display,
audio devices/server sockets, network, user media, credentials or other projects.
Require a complete case receipt, keep actual process exits, then join the owned
child. Admit32MiB individual files for first-run profile extraction after the
observed8MiB cap refusal. This is a fixture-authoring constraint, not a product
recording policy or hard RSS guarantee.

The product/CI reuse existing C++ inspection, crypto and Python acceptance
infrastructure; no new application library, driver or proprietary runtime
dependency is adopted. Windows can inspect the frozen Linux bytes independently;
a Windows source-writer corpus and full conversion/render acceptance remain gates.
