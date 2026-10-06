# Pinned equipment-profile reuse

Source: soundcurrent-eq commit `6b53056`, resolved exactly in [provenance](provenance.json).
Unmodified source/header, response-math reference, collector, catalog/license/report
and source-registry snapshots are retained under `upstream/`. Only the catalog resource
is compiled from that directory; upstream C++ is not compiled. The adapted editor is
in `ui/equipment_profiles.cpp`, with curve-only mathematics in `ui/equipment_curve.cpp`.
Original GPL-3.0-only SPDX notices remain; modified files name the date/provenance.
There was no additional file-level copyright declaration in these C++ files.

The root build requires no source equalizer checkout or runtime network collection.
The application reads a separate DAW user library. The copied collector is provenance
only; do not run it here to mutate snapshots. A catalog update requires a new reviewed
snapshot, hashes and acceptance evidence. The Pyle response arrays, proprietary plots,
and manufacturer/serial calibration files are not copied. Retained source metadata is
not permission to redistribute their measurements.

Current adapter remains Qt-specific and offline. It is not engine profile routing.
Shared schema/preparation/monitor paths and project pins need their own contracts/tests.
Improvements suitable for later equalizer adoption are listed in the manifest; nothing
is automatically written back to either equalizer repository.
