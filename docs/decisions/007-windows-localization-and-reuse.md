# ADR-007: Windows, all-Europe localization, and adapted shared code

Date: 2026-10-05. Status: accepted product requirements; implementation staged.

The owner requested Windows alongside Linux, clarified localization as **all of Europe**, and authorized adapting borrowed equalizer code in the DAW before returning useful changes to the equalizers later.

## Decisions

- Keep the frozen Reference A 6.1.3 / Reference B 15.0.30 references unchanged. Windows and localization are additional SoundCurrent requirements, not an assertion about either reference's language coverage.
- Windows must reach the same functional acceptance workflows as Linux. Platform build success does not prove device, MIDI, plugin, UI or recording parity. Linux remains first for native audio experiments.
- Keep session, graph, processors and offline rendering independent of Qt and OS audio headers. Native backend adapters use existing audio infrastructure. Evaluate WASAPI shared/exclusive and lawful ASIO integration; a virtual cable is not a prerequisite for ordinary DAW I/O.
- Treat national, non-EU and regional/minority European languages as part of the localization program. The initial register is extensible and is not a claim of exhaustive language coverage or completed translation.
- Persist stable IDs, units and UTF-8 text, never translated keys, localized decimal strings or GUI labels. Qt catalogs and locale formatting belong to the GUI. Every language has independent translation and review status.
- Borrow GPL-compatible equalizer code by copying an auditable subset into this repository and adapt it here. Keep origin repository, exact source revision, file hashes, original notices, changes and independent tests in a provenance manifest. Later upstream work can carry improvements back; no equalizer edits are part of this turn.

## Consequences

Windows cross-compilation starts now. Native Windows tests, installer and UI translation gates start as their components exist. There are no finished translations or new audio backends in the current core. Future copied DSP must remove DAW-inappropriate final clipping/headroom behavior and replace concurrent whole-state configuration with prepared state and bounded events as ADR-006 specifies.
