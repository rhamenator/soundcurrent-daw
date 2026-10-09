# ADR 101: Editable raw stretch anchors and checked artifact adoption

2026-10-09. Status: core implementation; desktop and native qualification pending.

Persist an immutable raw-span anchor alongside the derived playback asset, with
canonical settings and exact physical-frame coordinates. Re-render from that raw
anchor after edits, retain previous derivatives for Undo and adopt through one
validated structural transaction. Reject stale clips/raw identities without
overwriting unrelated project edits. Schema 1.12 makes the processor/source
algorithm explicit and migrates older projects without manufacturing DSP state.

Use one framework-independent bounded parent protocol/verifier for the future
desktop supervisor. A child's completion JSON is evidence to inspect, not authority
to mutate Session: independently hash raw/audio bytes and decode/check all output
samples. Hold pinned handles and resource grants through verification. Root/epoch
barriers and whole-child memory admission remain responsibilities of supervision.

Extend approved WAVE validation to RF64 with checked size tables and direct bounded
PCM/IEEE decoding. An actual libsndfile 1.2.2 failure on valid odd-length ancillary
RF64 chunks makes trusting that decoder alone insufficient here. Preserve original
bytes and the existing RIFF/RIFX path. This is not a claim that all playback consumers
now accept every RF64 file or that opaque codecs are supported.

Keep the frozen Reference A/B scope and F/Q/C/N gates unchanged. Exact state,
artifact integrity and live/export equivalence do not establish stretch quality,
desktop usability, Windows installation or full pitch/warp editing. See
[checkpoint 135](../135-editable-stretch-state.md) for scope and remaining tasks.
