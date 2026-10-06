# ADR-042: Persist desired punch locators and prepare from canonical state

Date: 2026-10-06. Status: accepted implementation; native/full punch parity open.

Store desired project-frame locators in schema 1.4. Default older projects to
disabled punch without inferring a window. Preserve disabled locators and exact
integer frames. Reject unknown schemas/fields instead of silently losing settings.
Use the existing transactional structural history and bounded control command
queue for settings, dirty state, undo/redo, barriers and Save/reopen.

The desktop edits canonical settings and prepares from its accepted barrier
snapshot. Require shared project playback for punch, explicit route selection and
immutable generation bounds. Query the endpoint's actual prepared playback end
to show latency postroll; reject invalid endpoint metadata. Retire incompatible
prepared generations when canonical locators change through another control path.
No callback-side model mutation or extra virtual dispatch is introduced.

Keep raw journal schema, supplied-latency attachment and native join/disk receipt
ownership unchanged. Qualify native, physical and Windows behavior independently;
project-frame controls do not establish tempo/beat/manual/Auto/loop/take/comping
or full frozen-reference parity.
