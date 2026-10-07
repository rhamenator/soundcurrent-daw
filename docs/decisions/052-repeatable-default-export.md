# ADR 052: omit implicit wall-clock PEAK metadata in default exports

Status: accepted, 2026-10-07 UTC. No new dependency or project/journal schema.

Default float WAV exports disable libsndfile's optional timestamp-bearing PEAK
chunk; RF64 keeps its default absence of that chunk. Avoid issuing the disable
command for absent RF64 peak_info, which libsndfile1.2.2 instead allocates. Preserve the existing peak/headroom result measurements and floating audio.
This makes repeated default renders byte-repeatable across wall-clock seconds
without weakening sample or file-hash checks. Explicit future metadata workflows
will define their own stable inputs. Recording media is unaffected. See
[replay and regression evidence](../66-repeatable-export.md).
