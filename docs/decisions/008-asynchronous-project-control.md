# ADR-008: Desktop control actor and separate project I/O

Status: **implemented foundation for S6c**,2026-10-05.

Use Qt QThread/QMutex/QWaitCondition only in the desktop adapter; engine/session/media remain framework independent. One control actor owns the editable model and scalar history. A separate single-flight worker owns blocking create/open/save. A bounded64-command non-RT FIFO and one latest immutable snapshot connect GUI to control. This avoids unbounded queued snapshot events and prevents disk work from blocking model edits during save.

Save records both captured revision and immutable content; dirty status compares current content to the successful saved snapshot. A close barrier establishes the accepted command prefix before a dirty decision. Priority shutdown is independent of queue capacity; I/O cancellation occurs at cooperative publication boundaries and final joins run on control, not audio or normal GUI event handling.

Qt Core/Gui/Widgets6.10.2 are system-linked in this development build; Test is a fixture-only module. The earlier dependency/license evaluation selects these modules under compatible open terms for GPL3 distribution; exact runtime/transitive notices, source delivery and Windows deployment still require package audit. No Qt source or binary is vendored here. A6.4 API floor is declared but not yet qualified across distribution versions.

This actor currently handles project editing only. Production native transport and asynchronous graph preparation will extend the control/preparation interfaces; callbacks must not consume these mutex queues. GUI commands must be translated into the existing bounded engine ingress, with explicit admission/reconciliation and off-RT retirement. Full graph/automation/general undo/monitoring and multitrack state remain staged requirements.
