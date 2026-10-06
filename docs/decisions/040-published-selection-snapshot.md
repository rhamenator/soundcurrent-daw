# ADR-040: Resolve programmatic selection against the published project

Date: 2026-10-06. Status: accepted; scoped Linux UI qualification.

The controller's Open completion and the GUI timer are independent. A published
track ID must not be refused merely because the timeline has not polled it.
Synchronize through the existing bounded GUI poll before selection, retaining
stable IDs and existing focus/selection callbacks. Do not wait for a worker or
implicitly start recording/playback.

Pass the captured canonical snapshot into the inspector projection during that
timeline redraw. Two independent reads can straddle Open completion and display
different project generations. Existing control/audio ownership remains intact.

Deterministic before-poll selection and requested playback preparation pass;
Debug desktop groups, Release and full sanitizers pass. The earlier unidentified
admission failure remains retained; Windows execution and full parity are not
inferred from these results. No engine, schema, dependency or equalizer change.
