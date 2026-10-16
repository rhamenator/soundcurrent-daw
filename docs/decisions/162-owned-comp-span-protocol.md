# ADR 162: owned exact-span process protocol

Status: accepted worker/candidate-integrity path; integration/adoption/quality open.

Use a separate versioned span protocol with exact recipe-derived grid geometry.
Keep parent recipe executor and span binary identities distinct. Bind exact request
bytes through a trusted control digest; hash the actual executable, apply process
memory/source/output/deadline grants, admit sources and require parent start after
verified readiness. Keep allocations/I/O/R3 outside audio and GUI callbacks.

Publish RF64 and completion through owned no-overwrite files with explicit flush
and cooperative lifetime locking. Retain incomplete states for recovery. Parent
verification after reaping must compare original intent/grants and independently
hash/decode all output PCM and recheck sources/markers. Active children and reused
operations must not be admitted as fresh completions.

An integrity-checked span remains a candidate. Pair/controller/recovery integration,
nonuniform production, blend/curve/alignment, post-SRC shared representation and
sound quality are required before model adoption. Preserve originals/anchors,
paired Undo and epoch/root/cancel/close/reopen boundaries. Keep prior spectral
artifact observations visible; executable identity and a valid marker do not
establish DSP quality or frozen-reference parity.
