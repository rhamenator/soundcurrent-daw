# Native restoration image and repository-source evidence

The installed controller tests establish four positive workflows and retain
executable hashes. The collection step additionally preserves the actual four
production executable images so future package qualification can independently
rehash the bytes that match those observations.

`tools/restoration_worker_images.py` captures the clean requested source commit,
tree and tracked build-input hashes before compilation. Linux now explicitly
checks out the PR head for this capture; Windows uses its existing native
pre-build source capture. Collection refuses changed inputs, wrong CMake roots,
missing or altered role images, wrong native amd64 headers, mismatched controller
PID/exit/source/command/output records, or an existing output directory. It copies
the four images and positive workflow records, then rechecks the inputs and
images before writing a success-only collection manifest. CMake configuration
identity is recorded without copying arbitrary cache variables.

The repository build-source archive contains the captured source, vendored code,
fixtures, licenses and scripts. Historical `tests/results` media is excluded,
matching the existing preview source collector; it remains available at the
recorded full commit. The archive is reread to verify the exact entry set and
every captured input digest. This is repository-source evidence; external
dependency-source bundles and standalone dependency qualification remain open.

On Linux, collection runs only on the shard with the successful positive
controller receipt. Other shards validate their source capture and explicitly
produce no collection. Windows collection runs after the native desktop main
suite. Both retain the material under `restoration-worker-images` in the existing
original CI artifact, alongside refusal and positive receipts. Final audit must
cross-bind image hashes to both native receipt sets on both platforms; collection
alone grants no qualification or authority.

These are development build inputs, not default preview payloads, installers or
releases. The manifest explicitly leaves dependency-source, standalone bundle,
installer and installed-GUI qualification false. The existing strict preview file
sets, PE/dependency allowlists, GPL delivery and installer lifecycle gates remain.
