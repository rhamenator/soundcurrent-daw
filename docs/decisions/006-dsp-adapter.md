# ADR-006: Adapt DSP reuse to DAW headroom and lifetime

Status: **proposed; partly supported by probe**. Date: 2026-10-05.

The public EQ Linux path manages PipeWire filter chains. The private Studio SDK independently provides portable C++ processing. Reuse its tested EQ math/control semantics, not the assumption that either existing app is a DAW engine.

The Studio processor's final sample clamp and automatic EQ headroom are inappropriate as unconditional per-track DAW behavior. A separate GPL DAW adapter/module must preserve float overs and implement parameter smoothing, stable band IDs, latency/tail descriptors and prepared state transitions. It must not modify the active equalizer repository.

The probe showed partition-independent EQ and immediate scalar gain with no ordinary new calls. It did not prove click-free frequency automation, large-state migration, delay compensation, real-time deadlines or recording.
