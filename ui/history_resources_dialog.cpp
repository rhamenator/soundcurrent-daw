// SPDX-License-Identifier: GPL-3.0-only
#include "history_resources_dialog.hpp"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QScrollArea>
#include <QScreen>
#include <QVBoxLayout>
#include <limits>
namespace soundcurrent::daw::ui {
namespace {
constexpr std::size_t mib = 1024 * 1024;
std::optional<std::size_t> number(const QString &text, std::size_t unit = 1) {
    bool ok = false;
    const auto n = QLocale().toULongLong(text, &ok);
    if (!ok || !n || n > std::numeric_limits<std::size_t>::max() / unit)
        return {};
    return static_cast<std::size_t>(n) * unit;
}
QString display(std::size_t n) {
    return QLocale().toString(static_cast<qulonglong>(n));
}
} // namespace
HistoryBudget loadHistoryPreferences() {
    QSettings settings;
    HistoryBudget defaults, result;
    auto read = [&](const char *key, std::size_t fallback) {
        bool ok = false;
        const auto value =
            settings.value(QString::fromLatin1(key), static_cast<qulonglong>(fallback))
                .toULongLong(&ok);
        return ok && value && value <= std::numeric_limits<std::size_t>::max()
                   ? static_cast<std::size_t>(value)
                   : fallback;
    };
    result.retainedBytes = read("history/retainedBytes", defaults.retainedBytes);
    result.operationBytes = read("history/operationBytes", defaults.operationBytes);
    result.maximumCommands = read("history/maximumCommands", defaults.maximumCommands);
    return result;
}
void saveHistoryPreferences(HistoryBudget budget) {
    validateHistoryBudget(budget);
    QSettings settings;
    settings.setValue("history/retainedBytes", static_cast<qulonglong>(budget.retainedBytes));
    settings.setValue("history/operationBytes", static_cast<qulonglong>(budget.operationBytes));
    settings.setValue("history/maximumCommands", static_cast<qulonglong>(budget.maximumCommands));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw ProjectError(ErrorCode::Io, "Could not save Undo preferences");
}
MemoryPreferences loadMemoryPreferences() {
    QSettings settings;
    MemoryPreferences result;
    auto read = [&](const char *key, std::size_t fallback) {
        bool ok = false;
        const auto value =
            settings.value(QString::fromLatin1(key), static_cast<qulonglong>(fallback))
                .toULongLong(&ok);
        return ok && value && value <= std::numeric_limits<std::size_t>::max() ? std::size_t(value)
                                                                               : fallback;
    };
    result.totalBytes = read("memory/totalBytes", result.totalBytes);
    result.snapshotBytes = read("memory/snapshotBytes", result.snapshotBytes);
    return result;
}
void saveMemoryPreferences(MemoryPreferences policy) {
    if (!policy.totalBytes || !policy.snapshotBytes)
        throw ProjectError(ErrorCode::InvalidParameter, "Memory limits must be positive");
    QSettings settings;
    settings.setValue("memory/totalBytes", static_cast<qulonglong>(policy.totalBytes));
    settings.setValue("memory/snapshotBytes", static_cast<qulonglong>(policy.snapshotBytes));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw ProjectError(ErrorCode::Io, "Could not save memory preferences");
}
HistoryResourcesDialog::HistoryResourcesDialog(
    ProjectController &controller, std::uint64_t &sequence,
    std::function<void(HistoryBudget)> accepted, QWidget *parent,
    std::function<void(MemoryPreferences)> memoryAccepted)
    : QDialog(parent) {
    setObjectName("historyResourcesDialog");
    setWindowTitle(tr("Project resources"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("projectResourcesScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *layout = new QFormLayout(page);
    scroll->setWidget(page);
    outer->addWidget(scroll, 1);
    auto *explanation =
        new QLabel(tr("Limits apply to Undo payload and declared state/edit work. "
                      "They do not measure total RAM. New edits may retire the "
                      "oldest Undo commands; reducing limits never clears existing history."),
                   this);
    explanation->setWordWrap(true);
    layout->addRow(explanation);
    auto *count = new QLineEdit(this);
    auto *retained = new QLineEdit(this);
    auto *operation = new QLineEdit(this);
    count->setObjectName("historyCommandLimit");
    retained->setObjectName("historyRetainedMiB");
    operation->setObjectName("historyOperationMiB");
    const auto policy = controller.snapshot()->historyBudget;
    count->setText(display(policy.maximumCommands));
    // Byte-exact configured policies remain unchanged unless the user edits these fields.
    retained->setText(display(policy.retainedBytes / mib));
    retained->setToolTip(tr("Current limit: %1 bytes. Edited values use whole MiB.")
                             .arg(display(policy.retainedBytes)));
    operation->setText(display(policy.operationBytes / mib));
    operation->setToolTip(tr("Current limit: %1 bytes. Edited values use whole MiB.")
                              .arg(display(policy.operationBytes)));
    for (auto *field : {count, retained, operation})
        field->setMaxLength(64);
    layout->addRow(tr("Maximum retained commands"), count);
    layout->addRow(tr("Retained payload (MiB)"), retained);
    layout->addRow(tr("Operation workspace (MiB)"), operation);
    auto *usage = new QLabel(this);
    usage->setObjectName("historyUsage");
    usage->setWordWrap(true);
    layout->addRow(usage);
    auto *snapshotUsage = new QLabel(this);
    snapshotUsage->setObjectName("snapshotUsage");
    snapshotUsage->setWordWrap(true);
    layout->addRow(snapshotUsage);
    auto *feedback = new QLabel(this);
    feedback->setObjectName("historyFeedback");
    feedback->setWordWrap(true);
    layout->addRow(feedback);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    outer->addWidget(buttons);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName("historyApply");
    connect(buttons, &QDialogButtonBox::rejected, this, &HistoryResourcesDialog::reject);
    connect(apply, &QPushButton::clicked, this,
            [&, this, count, retained, operation, feedback, apply, buttons, policy] {
                if (pending_ || memoryPending_)
                    return;
                const auto commands = number(count->text());
                const auto retainedBytes = retained->isModified()
                                               ? number(retained->text(), mib)
                                               : std::optional(policy.retainedBytes);
                const auto operationBytes = operation->isModified()
                                                ? number(operation->text(), mib)
                                                : std::optional(policy.operationBytes);
                if (!commands || !retainedBytes || !operationBytes) {
                    feedback->setText(
                        tr("Enter positive whole numbers within this machine's size range."));
                    return;
                }
                if (sequence == std::numeric_limits<std::uint64_t>::max()) {
                    feedback->setText(tr("Resource request sequence exhausted."));
                    return;
                }
                ProjectCommand command{CommandKind::HistoryLimits};
                command.historyBudget = HistoryBudget{*retainedBytes, *operationBytes, *commands};
                command.historyRequest = sequence++;
                if (controller.submit(command) != Admission::Accepted) {
                    feedback->setText(tr("The project worker is busy or closing. Try again."));
                    return;
                }
                pending_ = command.historyRequest;
                feedback->setText(tr("Applying limits…"));
                apply->setEnabled(false);
                buttons->button(QDialogButtonBox::Close)->setEnabled(false);
                for (auto *field : {count, retained, operation})
                    field->setEnabled(false);
            });
    auto *memoryExplanation =
        new QLabel(tr("The shared project budget covers canonical state, snapshots, Undo, declared "
                      "edit work, list/timeline indices, projections, prepared DSP, readers, media "
                      "caches, export buffers, capture pools and writer/hash/journal workspace. "
                      "New preparation follows this trusted budget. Parser/other IO, "
                      "copied owner sessions and Qt/allocator overhead remain separate."),
                   this);
    memoryExplanation->setWordWrap(true);
    layout->addRow(memoryExplanation);
    const auto totalInitial = controller.memoryResources().limitBytes;
    const auto snapshotInitial = controller.snapshotResources().limitBytes;
    auto *totalField = new QLineEdit(display(totalInitial / mib), this);
    auto *snapshotField = new QLineEdit(display(snapshotInitial / mib), this);
    totalField->setObjectName("projectMemoryMiB");
    snapshotField->setObjectName("snapshotMemoryMiB");
    for (auto *field : {totalField, snapshotField})
        field->setMaxLength(64);
    totalField->setToolTip(
        tr("Current limit: %1 bytes. Edited values use whole MiB.").arg(display(totalInitial)));
    snapshotField->setToolTip(
        tr("Current limit: %1 bytes. Edited values use whole MiB.").arg(display(snapshotInitial)));
    layout->addRow(tr("Shared project budget (MiB)"), totalField);
    layout->addRow(tr("Retained snapshot budget (MiB)"), snapshotField);
    auto *memoryUsage = new QLabel(this);
    memoryUsage->setObjectName("projectMemoryUsage");
    memoryUsage->setWordWrap(true);
    layout->addRow(memoryUsage);
    auto *memoryFeedback = new QLabel(this);
    memoryFeedback->setObjectName("projectMemoryFeedback");
    memoryFeedback->setWordWrap(true);
    layout->addRow(memoryFeedback);
    auto *memoryApply = new QPushButton(tr("Apply memory limits"), this);
    memoryApply->setObjectName("projectMemoryApply");
    layout->addRow(memoryApply);
    connect(
        memoryApply, &QPushButton::clicked, this,
        [&, this, totalField, snapshotField, totalInitial, snapshotInitial, memoryFeedback,
         memoryApply, apply, buttons, count, retained, operation] {
            if (pending_ || memoryPending_)
                return;
            const auto total = totalField->isModified() ? number(totalField->text(), mib)
                                                        : std::optional(totalInitial);
            const auto snapshots = snapshotField->isModified() ? number(snapshotField->text(), mib)
                                                               : std::optional(snapshotInitial);
            if (!total || !snapshots) {
                memoryFeedback->setText(
                    tr("Enter positive whole numbers within this machine's size range."));
                return;
            }
            if (sequence == std::numeric_limits<std::uint64_t>::max()) {
                memoryFeedback->setText(tr("Resource request sequence exhausted."));
                return;
            }
            ProjectCommand command{CommandKind::MemoryLimits};
            command.memoryBytes = *total;
            command.snapshotBytes = *snapshots;
            command.memoryRequest = sequence++;
            if (controller.submit(command) != Admission::Accepted) {
                memoryFeedback->setText(tr("The project worker is busy or closing. Try again."));
                return;
            }
            memoryPending_ = command.memoryRequest;
            memoryFeedback->setText(tr("Applying memory limits…"));
            memoryApply->setEnabled(false);
            apply->setEnabled(false);
            buttons->button(QDialogButtonBox::Close)->setEnabled(false);
            for (auto *field : {count, retained, operation, totalField, snapshotField})
                field->setEnabled(false);
        });
    auto *timer = new QTimer(this);
    connect(
        timer, &QTimer::timeout, this,
        [&, this, usage, snapshotUsage, feedback, apply, buttons, count, retained, operation,
         accepted, memoryApply, memoryFeedback, memoryUsage, totalField, snapshotField,
         memoryAccepted] {
            const auto snapshot = controller.snapshot();
            const auto reservations = controller.snapshotResources();
            snapshotUsage->setText(
                tr("Project snapshot reservations: %1 owners\n"
                   "%2 / %3 bytes · Peak reservation: %4 bytes")
                    .arg(display(reservations.owners), display(reservations.reservedBytes),
                         display(reservations.limitBytes), display(reservations.peakBytes)));
            const auto parent = controller.memoryResources();
            memoryUsage->setText(tr("Shared reservations: %1 / %2 bytes · Peak: %3 "
                                    "bytes\nCanonical: %4 bytes · History: %5 bytes")
                                     .arg(display(parent.reservedBytes), display(parent.limitBytes),
                                          display(parent.peakBytes),
                                          display(snapshot->canonicalBytes),
                                          display(snapshot->historyBytes)));
            if (memoryPending_ && snapshot->memoryCompleted.request == memoryPending_) {
                memoryPending_ = 0;
                if (snapshot->memoryCompleted.error) {
                    memoryFeedback->setText(
                        tr("Memory limits were not changed: %1")
                            .arg(QString::fromUtf8(snapshot->memoryCompleted.diagnostic.c_str())));
                } else {
                    memoryFeedback->setText(
                        tr("Memory limits applied. Retained state is preserved."));
                    try {
                        if (memoryAccepted)
                            memoryAccepted({parent.limitBytes, reservations.limitBytes});
                    } catch (const std::exception &e) {
                        memoryFeedback->setText(
                            tr("Limits applied, but preferences could not be saved: %1")
                                .arg(QString::fromUtf8(e.what())));
                    }
                }
            }
            const bool busy = pending_ || memoryPending_;
            for (auto *field : {count, retained, operation, totalField, snapshotField})
                field->setEnabled(!busy);
            apply->setEnabled(!busy);
            memoryApply->setEnabled(!busy);
            buttons->button(QDialogButtonBox::Close)->setEnabled(!busy);
            const auto &r = snapshot->historyResources;
            usage->setText(tr("Undo: %1 · Redo: %2\nRetained: %3 bytes · Active: %4 bytes\n"
                              "Accepted operation peak: %5 bytes · Retired commands: %6")
                               .arg(display(r.undoCommands), display(r.redoCommands),
                                    display(r.retainedBytes), display(r.activeBytes),
                                    display(r.operationPeakBytes), display(r.evictedCommands)));
            if (!pending_ || snapshot->historyCompleted.request != pending_)
                return;
            pending_ = 0;
            apply->setEnabled(true);
            buttons->button(QDialogButtonBox::Close)->setEnabled(true);
            for (auto *field : {count, retained, operation})
                field->setEnabled(true);
            if (snapshot->historyCompleted.error) {
                feedback->setText(
                    tr("Limits were not changed: %1")
                        .arg(QString::fromUtf8(snapshot->historyCompleted.diagnostic.c_str())));
            } else {
                feedback->setText(tr("Limits applied. Existing Undo and Redo are preserved."));
                try {
                    if (accepted)
                        accepted(snapshot->historyBudget);
                } catch (const std::exception &e) {
                    feedback->setText(tr("Limits applied, but preferences could not be saved: %1")
                                          .arg(QString::fromUtf8(e.what())));
                }
            }
        });
    timer->start(20);
    const auto available = screen()->availableGeometry();
    resize(std::max(240, std::min(560, available.width() - 80)),
           std::max(240, std::min(720, available.height() - 80)));
}
void HistoryResourcesDialog::reject() {
    if (!pending_ && !memoryPending_)
        QDialog::reject();
}
void HistoryResourcesDialog::closeEvent(QCloseEvent *event) {
    if (pending_ || memoryPending_)
        event->ignore();
    else
        QDialog::closeEvent(event);
}
} // namespace soundcurrent::daw::ui
