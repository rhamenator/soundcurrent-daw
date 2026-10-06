// SPDX-License-Identifier: GPL-3.0-only
#include "recovery_controller.hpp"
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <limits>
namespace soundcurrent::daw::ui {
struct RecoveryController::State : QThread {
    struct Request {
        std::filesystem::path root;
        std::shared_ptr<const Session> session;
        std::uint64_t epoch, serial;
    };
    mutable QMutex mutex;
    QWaitCondition wake;
    std::optional<Request> pending;
    std::atomic<bool> closing{false};
    std::atomic<std::uint64_t> serial{0};
    RecoveryScanOptions options;
    std::shared_ptr<const RecoveryScanSnapshot> latest =
        std::make_shared<const RecoveryScanSnapshot>();
    explicit State(RecoveryScanOptions o) : options(std::move(o)) {}
    bool canceled(std::uint64_t s) const noexcept {
        return closing || serial != s;
    }
    void run() override {
        for (;;) {
            std::optional<Request> q;
            {
                QMutexLocker guard(&mutex);
                while (!pending && !closing)
                    wake.wait(&mutex);
                if (closing)
                    break;
                q = std::move(pending);
                pending.reset();
                RecoveryScanSnapshot v;
                v.root = q->root;
                v.projectEpoch = q->epoch;
                v.serial = q->serial;
                v.running = true;
                latest = std::make_shared<const RecoveryScanSnapshot>(std::move(v));
            }
            RecoveryScanSnapshot result;
            result.root = q->root;
            result.projectEpoch = q->epoch;
            result.serial = q->serial;
            try {
                if (options.beforeScan)
                    options.beforeScan();
                auto discovery = options.discovery;
                const auto old = discovery.boundary;
                discovery.boundary = [&, old] {
                    if (canceled(q->serial))
                        throw ProjectError(ErrorCode::Canceled, "Recording discovery canceled");
                    if (old)
                        old();
                };
                result.discovery = std::make_shared<const RecordingDiscovery>(
                    discoverRecordings(q->root, *q->session, discovery));
            } catch (const ProjectError &e) {
                result.errorCode = e.code();
                result.diagnostic = e.what();
            } catch (const std::exception &e) {
                result.errorCode = ErrorCode::Io;
                result.diagnostic = e.what();
            }
            {
                QMutexLocker guard(&mutex);
                if (!canceled(q->serial))
                    latest = std::make_shared<const RecoveryScanSnapshot>(std::move(result));
                else if (!pending) {
                    auto v = *latest;
                    v.running = false;
                    v.discovery.reset();
                    v.errorCode.reset();
                    v.diagnostic.clear();
                    latest = std::make_shared<const RecoveryScanSnapshot>(std::move(v));
                }
            }
        }
        QMutexLocker guard(&mutex);
        auto v = *latest;
        v.running = false;
        v.closed = true;
        latest = std::make_shared<const RecoveryScanSnapshot>(std::move(v));
    }
};
RecoveryController::RecoveryController(RecoveryScanOptions o)
    : state_(std::make_unique<State>(std::move(o))) {
    state_->start();
}
RecoveryController::~RecoveryController() {
    requestShutdown();
    state_->wait();
}
bool RecoveryController::scan(std::filesystem::path root, std::shared_ptr<const Session> session,
                              std::uint64_t epoch) {
    QMutexLocker guard(&state_->mutex);
    if (state_->closing || !session || !epoch || state_->serial == UINT64_MAX)
        return false;
    const auto serial = ++state_->serial;
    state_->pending = State::Request{std::move(root), std::move(session), epoch, serial};
    state_->wake.wakeOne();
    return true;
}
void RecoveryController::cancel() noexcept {
    QMutexLocker guard(&state_->mutex);
    if (state_->serial != UINT64_MAX)
        ++state_->serial;
    else
        state_->closing = true; // Exhaustion cannot leave an uncancelable last request.
    state_->pending.reset();
    state_->wake.wakeOne();
}
void RecoveryController::requestShutdown() noexcept {
    QMutexLocker guard(&state_->mutex);
    state_->closing = true;
    state_->pending.reset();
    state_->wake.wakeOne();
}
std::shared_ptr<const RecoveryScanSnapshot> RecoveryController::snapshot() const {
    QMutexLocker guard(&state_->mutex);
    return state_->latest;
}
} // namespace soundcurrent::daw::ui
