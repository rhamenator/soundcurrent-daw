# ADR 050: manual recording consumers and grouped results

Status: accepted, 2026-10-06. GPL-3.0-only. No new dependency.

## Context

ADR 049 provides manual-punch engine slots but requires callers to join disk
consumers before releasing them. Production control needs to enforce this,
handle late worker startup, preserve initiating failures and avoid fake empty
assets while the same playback/EQ generation remains live.

## Decision

Introduce framework-independent `ManualRecordingRun` as the serialized off-audio
consumer owner. Audio publishes exact immutable accepted raw extents before
retirement; control never samples an audio-only cursor. Consumers bind resolved
configs, join/release leases before verification/reclamation, and expose bounded
immutable grouped results. Application reply credits last until explicit consume,
and eight pending/unconsumed groups apply honest backpressure.

Classify Empty, Complete, Failed and Canceled per lane. Retain independently
verified checkpoints, original exceptions and verification exceptions separately.
Require explicit partial adoption for interruption/missing lanes. Keep the current
playback graph immutable across canonical grouped edits; prepare a new generation
only through the transport owner. Callback owner must join before shutdown or
object destruction. Existing PipeWire is used by the subsequent native adapter.

## Consequences

The disk/control worker can perform IO, construction and joins without blocking
GUI or callback. Its service cadence must be admitted against prepared capture
reserve; synthetic acceptance establishes ownership/functionality, not sustained
native deadlines. Native/Qt and Windows owners must implement the specified
join, progress and canonical-edit rules. Full finite/indefinite recording parity
remains open. See [contract](../64-manual-recording-control-owner.md).
