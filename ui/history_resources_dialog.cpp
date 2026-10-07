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
HistoryResourcesDialog::HistoryResourcesDialog(ProjectController &controller,
                                               std::uint64_t &sequence,
                                               std::function<void(HistoryBudget)> accepted,
                                               QWidget *parent)
    : QDialog(parent) {
    setObjectName("historyResourcesDialog");
    setWindowTitle(tr("Undo resources"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);
    auto *layout = new QFormLayout(this);
    auto *explanation = new QLabel(
        tr("Limits apply to Undo payload and declared state/edit work. "
           "They do not measure total RAM, audio graphs or GUI memory. New edits may retire the "
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
    layout->addRow(buttons);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName("historyApply");
    connect(buttons, &QDialogButtonBox::rejected, this, &HistoryResourcesDialog::reject);
    connect(apply, &QPushButton::clicked, this,
            [&, this, count, retained, operation, feedback, apply, buttons, policy] {
                if (pending_)
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
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this,
            [&, this, usage, snapshotUsage, feedback, apply, buttons, count, retained, operation,
             accepted] {
                const auto snapshot = controller.snapshot();
                const auto reservations = controller.snapshotResources();
                snapshotUsage->setText(
                    tr("Project snapshot reservations: %1 owners\n"
                       "%2 / %3 bytes · Peak reservation: %4 bytes")
                        .arg(display(reservations.owners), display(reservations.reservedBytes),
                             display(reservations.limitBytes), display(reservations.peakBytes)));
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
                        feedback->setText(
                            tr("Limits applied, but preferences could not be saved: %1")
                                .arg(QString::fromUtf8(e.what())));
                    }
                }
            });
    timer->start(20);
    resize(520, sizeHint().height());
}
void HistoryResourcesDialog::reject() {
    if (!pending_)
        QDialog::reject();
}
void HistoryResourcesDialog::closeEvent(QCloseEvent *event) {
    if (pending_)
        event->ignore();
    else
        QDialog::closeEvent(event);
}
} // namespace soundcurrent::daw::ui
