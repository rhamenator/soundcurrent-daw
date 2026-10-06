// SPDX-License-Identifier: GPL-3.0-only
#include "export_controller.hpp"
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <chrono>
#include <limits>
namespace soundcurrent::daw::ui {
struct ExportController::State : QThread {
    mutable QMutex mutex;
    QWaitCondition wake;
    std::optional<ExportRequest> queued;
    std::optional<bool> decision;
    std::atomic<bool> canceled{false}, closing{false};
    bool busy = false, awaiting = false;
    std::uint64_t sequence = 0, errors = 0;
    std::shared_ptr<const ExportSnapshot> latest = std::make_shared<const ExportSnapshot>();
    ExportSnapshot view;
    ExportControllerOptions options;
    explicit State(ExportControllerOptions value) : options(std::move(value)) {}
    void checkCanceled() {
        if (closing.load(std::memory_order_acquire) || canceled.load(std::memory_order_acquire) ||
            (options.render.canceled && options.render.canceled()))
            throw ProjectError(ErrorCode::Canceled, "Export canceled before publication");
    }
    void publish() {
        auto next = std::make_shared<const ExportSnapshot>(view);
        QMutexLocker lock(&mutex);
        latest = std::move(next);
    }
    void execute(ExportRequest &job) {
        checkCanceled();
        if (!job.session || !job.modelRevision || job.spec.replaceSha256)
            throw ProjectError(
                ErrorCode::InvalidState,
                "Export needs an immutable model/revision and controller-owned consent");
        validate(*job.session);
        view.phase = ExportPhase::Inspecting;
        publish();
        if (options.beforeInspect)
            options.beforeInspect();
        checkCanceled();
        auto destination =
            inspectExportDestination(job.root, job.destination, [&] { checkCanceled(); });
        view.destination = destination.path;
        job.destination = destination.path;
        if (destination.exists) {
            view.replacement = destination;
            view.phase = ExportPhase::AwaitingConfirmation;
            {
                QMutexLocker lock(&mutex);
                awaiting = true;
            }
            publish();
            bool approved = false;
            {
                QMutexLocker lock(&mutex);
                while (!decision && !closing.load() && !canceled.load()) {
                    wake.wait(&mutex, 50);
                    lock.unlock();
                    checkCanceled();
                    lock.relock();
                }
                if (decision)
                    approved = *decision;
                decision.reset();
            }
            checkCanceled();
            if (!approved)
                throw ProjectError(ErrorCode::Canceled, "Existing file kept; export canceled");
            job.spec.replaceSha256 = destination.sha256;
        }
        checkCanceled();
        view.phase = ExportPhase::Rendering;
        publish();
        if (options.beforeRender)
            options.beforeRender(job);
        checkCanceled();
        auto render = options.render;
        render.canceled = [&] {
            return closing.load(std::memory_order_acquire) ||
                   canceled.load(std::memory_order_acquire) ||
                   (options.render.canceled && options.render.canceled());
        };
        auto lastProgress = std::chrono::steady_clock::now();
        render.progress = [&](Frame written, Frame maximum) {
            view.written = written;
            view.maximum = maximum;
            const auto now = std::chrono::steady_clock::now();
            if (now - lastProgress >= std::chrono::milliseconds(50)) {
                publish();
                lastProgress = now;
            }
            if (options.render.progress)
                options.render.progress(written, maximum);
        };
        auto result = exportTrackWav(job.root, *job.session, job.destination, job.spec, render);
        view.written = result.frames;
        view.maximum = result.frames;
        view.result = std::make_shared<const ExportResult>(std::move(result));
        view.phase = ExportPhase::Complete; // Published success wins a later cancel/close.
        view.replacement.reset();
    }
    void run() override {
        for (;;) {
            std::optional<ExportRequest> job;
            {
                QMutexLocker lock(&mutex);
                while (!queued && !closing.load())
                    wake.wait(&mutex);
                if (!queued)
                    break;
                job = std::move(queued);
                queued.reset();
                view = *latest;
            }
            try {
                execute(*job);
            } catch (const ProjectError &error) {
                view.errorCode = error.code();
                view.diagnostic = error.what();
                view.phase = error.code() == ErrorCode::Canceled ? ExportPhase::Canceled
                                                                 : ExportPhase::Fault;
                view.errorSerial = ++errors;
                view.replacement.reset();
            } catch (const std::exception &error) {
                view.errorCode = ErrorCode::Io;
                view.diagnostic = error.what();
                view.phase = ExportPhase::Fault;
                view.errorSerial = ++errors;
                view.replacement.reset();
            }
            job.reset(); // Release immutable model and finished core state before releasing
                         // admission.
            view.busy = false;
            {
                QMutexLocker lock(&mutex);
                latest = std::make_shared<const ExportSnapshot>(view);
                busy = false;
                awaiting = false;
                decision.reset();
                if (closing.load())
                    break;
            }
        }
        view.busy = false;
        view.phase = ExportPhase::Closed;
        view.closed = true;
        publish();
    }
};
ExportController::ExportController(ExportControllerOptions options)
    : state_(std::make_unique<State>(std::move(options))) {
    state_->start(QThread::LowPriority);
}
ExportController::~ExportController() {
    requestShutdown();
    state_->wait();
}
Admission ExportController::submit(ExportRequest request) {
    QMutexLocker lock(&state_->mutex);
    if (state_->closing.load())
        return Admission::Closing;
    if (state_->busy)
        return Admission::Full;
    if (state_->sequence == std::numeric_limits<std::uint64_t>::max())
        return Admission::Full;
    ExportSnapshot next;
    next.phase = ExportPhase::Queued;
    next.busy = true;
    next.job = ++state_->sequence;
    next.errorSerial = state_->errors;
    next.root = request.root;
    next.destination = request.destination;
    next.modelRevision = request.modelRevision;
    next.trackId = request.spec.trackId;
    if (request.session)
        next.projectId = request.session->id;
    state_->latest = std::make_shared<const ExportSnapshot>(std::move(next));
    state_->queued = std::move(request);
    state_->busy = true;
    state_->decision.reset();
    state_->awaiting = false;
    state_->canceled.store(false, std::memory_order_release);
    state_->wake.wakeOne();
    return Admission::Accepted;
}
bool ExportController::confirm(std::uint64_t job, bool replace) {
    QMutexLocker lock(&state_->mutex);
    if (!state_->busy || !state_->awaiting || state_->latest->job != job ||
        state_->latest->phase != ExportPhase::AwaitingConfirmation || state_->decision ||
        state_->canceled.load() || state_->closing.load())
        return false;
    state_->decision = replace;
    state_->awaiting = false;
    state_->wake.wakeOne();
    return true;
}
void ExportController::requestCancel() noexcept {
    QMutexLocker lock(&state_->mutex);
    if (state_->busy)
        state_->canceled.store(true, std::memory_order_release);
    state_->wake.wakeOne();
}
void ExportController::requestShutdown() noexcept {
    state_->closing.store(true, std::memory_order_release);
    state_->canceled.store(true, std::memory_order_release);
    QMutexLocker lock(&state_->mutex);
    state_->wake.wakeOne();
}
std::shared_ptr<const ExportSnapshot> ExportController::snapshot() const {
    QMutexLocker lock(&state_->mutex);
    const bool wanted = state_->latest->busy && state_->canceled.load();
    if ((state_->latest->closed && !state_->isFinished()) ||
        wanted != state_->latest->cancelRequested) {
        auto next = std::make_shared<ExportSnapshot>(*state_->latest);
        if (!state_->isFinished())
            next->closed = false;
        next->cancelRequested = wanted;
        return next;
    }
    return state_->latest;
}
} // namespace soundcurrent::daw::ui
