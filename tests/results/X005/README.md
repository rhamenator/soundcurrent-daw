# Equipment profile/editor evidence

- [Initial pinned editor](2026-10-05-equipment-editor.json): import, fitting, reference/custom-copy separation, dirty-close choices and DAW menu integration at `6b53056`.
- [Catalog and taxonomy update](2026-10-05-equipment-catalog-update.json): committed equipment update `459627c`, 1,092 catalog round trips, legacy metadata defaults, subtype/power filters and metadata undo/save. Two affected Linux UI groups pass debug and ASan/UBSan/LSan; no native Windows/audio qualification implied.

These are historical implementation checkpoints, not full X005 completion. See [the contract](../../../docs/17-equipment-profiles.md) for processing, portability, measurement, platform, localization and rights gaps. Snapshot hashes/revisions are independently checked against committed equalizer blobs; no equalizer checkout writes or publication occur.
