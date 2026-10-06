# S6f native recording ownership and verified take attachment

**Implemented Linux production owner and asynchronous attachment; Record/arm/input/monitor/recovery desktop controls are next. SLICE-001 and the full goal remain incomplete.** Equipment profiles remain an offline editor and do not alter raw takes or monitoring yet.

## Native and disk lifetime

`SoundCurrent::PipeWireRecording` is a Linux control/preparation owner using the existing PipeWire adapter, `AudioBridge`, `CapturePipe` and `RecordingWorker`. Construction validates session/track/layout/rate, project identity, capture bounds and supplied alignment, prepares EQ and storage, and publishes inactive ports. It creates no recording job before activation. The class may block during setup/join; call it from a desktop preparation worker, never widgets or audio callbacks.

All input channels must be selected explicitly. `RecordingMonitor::Off` exposes zero output ports and processes into preallocated discard buffers. `PostEq` requires explicit, complete output selection. No hardware route, system default, autoconnection, rate or quantum is changed. Stale/wrong-direction/repeated routes and activation before required selection fail. Routes cannot be changed after activation; no automatic device replacement is offered.

Activation constructs the disk worker immediately before activating native processing. Failure latches a terminal cause, joins native and writer owners, preserves the primary activation exception, and leaves the canonical project unchanged. Failure before the first journal publication can leave an owned zero-frame orphan directory; discovery/cleanup UI is still required. `jobDirectory()` returns no path when writer construction did not finish.

Stop is idempotent: latch stop or existing writer failure, disconnect/join native callbacks, finish the raw pipe, drain/finalize/join the disk worker, then expose the result or retained exception. Destruction follows normal stop; cancellation explicitly requests writer cancellation and retains a recoverable checkpoint. A naturally completed raw range does not certify disk success: `result()` after stop is authoritative. `writerComplete()` means terminal success or failure, not successful publication.

No ownership retirement, allocations, disk work, GUI calls or joins happen in the host callback. Audit hooks are immutable function/context pointers. Callback observations and immediate-edit receipts retain their bounded/loss-counted queues. Preparation and shutdown are currently single-control-owner APIs; callers must not concurrently mutate the owner.

## Raw input and sticky terminal causes

The bridge now copies raw input and computes its input peak **before EQ processing**, including when native output aliases input storage. The previous ordering could let in-place DSP contaminate a raw take. Raw invalid-sample sanitization remains explicit; internal floating-point overs are preserved. A later processor failure leaves the accepted raw prefix available.

Terminal arbitration uses a unified atomic state. Audio publication makes one strong CAS attempt. Control fault publication has at most two strong attempts to cover the only active transition, Ready to Running. Existing terminal causes cannot be overwritten by subsequent normal completion or stop. A fault that wins publication is retained in state, observations and journal end reason; a fault after completed range publication does not retroactively change that range. Writer errors remain independently authoritative even if capture completed.

## Attaching a finalized take

The desktop `ProjectController` accepts typed `AttachRecording` commands with a shared immutable receipt and exact current root. It first validates a candidate session on the control worker, then sends journal/media verification to its separate I/O worker. No speculative asset or clip is published.

Verification requires the owned `media/capture-UUID/take.wav` location, finalized journal, persisted project/track/asset/rate/layout/start/alignment/recovery identity, capture status and frame count. `ProjectStore::verifyMedia` validates the candidate and hashes its referenced files using the existing owner-controlled path rules. Runtime pool size, callback bound and memory budget are not journal fields and are not compared with recovery's pool settings.

On success, the control worker attaches to the latest canonical session, preserving EQ edits accepted while verification was pending. Attachment increments the model revision, exposes the attached asset identity/count, and makes the project dirty; saving is a separate operation. Duplicate receipts, cross-root receipts, altered hashes and wrong extents fail without changing canonical state. Current undo only covers scalar gestures and does not undo the new clip/asset.

Shutdown can cancel attachment while verification is in progress. The finalized take and journal remain recoverable. Before integrating Record into the desktop, its close workflow must stop/finalize recording and resolve pending attachment **before** the accepted-command/dirty barrier. Do not silently abandon an unsaved take on close or project replacement.

## Evidence and reproduction

[Result manifest](../tests/results/SLICE-001/2026-10-05-native-recording-owner.json) records exact source/binary/log hashes, Linux debug and ASan/UBSan CTest groups, Windows headless cross-build and serial native results.

```sh
ctest --test-dir .cache/build-desktop --output-on-failure
python3 tests/verify_pipewire_fixture.py \
  --binary .cache/build-desktop/sc-pipewire-fixture \
  --modes normal disconnect off stop cancel writer-failure destructor activation-failure
```

The native fixture now uses the production owner. Owned source/monitor sink comparisons verify ten seconds of raw audio, independent post-EQ output against private offline EQ, monitoring off, input removal, user stop, cancellation, injected disk failure, destructor finalization and failed activation. The harness observes pre-existing links/defaults and removal of owned routes. Bridge fixtures additionally test in-place raw input across 1/2/8/32/256 channels and concurrent fault/completion publication. Controller fixtures gate verification while editing, reject altered receipts, save/reopen, and cancel attachment during shutdown.

Native sanitizer results use the explicit diagnostic `PIPEWIRE_DLCLOSE=false` with leak detection enabled. The earlier normal module-unload leak report remains unresolved; this is not a production workaround or qualification pass. The previous overlapping-load native timeout also remains unresolved. Run native fixtures serially after builds/CTest.

No physical microphone/speaker test, measured input alignment, memory locking, deadline/load qualification, automatic reprepare/reconnect, native Windows audio or Windows Qt UI qualification is claimed. Physical disk-full/power-loss, >4 GiB RF64, multitrack admission, broad import sandboxing and filesystem race qualification remain open. No frozen parity row is complete.

## Next implementation task

Add an asynchronous recording desktop owner and explicit input selection, arm, Record/Stop, monitoring Off/Post-EQ, elapsed/input/output status and recovery discovery/preview. Reconcile live parameter receipts and canonical raw take attachment; qualify close/project replacement during setup, capture, disk finalization and verification. Then implement S7 transactional WAV export using a private instance of the shared engine.
