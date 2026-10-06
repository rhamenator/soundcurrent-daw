// SPDX-License-Identifier: GPL-3.0-only
#include "export_dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
namespace soundcurrent::daw::ui {
namespace {
QString text(const std::filesystem::path &path) {
    const auto value = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(value.data()),
                             static_cast<qsizetype>(value.size()));
}
std::filesystem::path path(const QString &text) {
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    return utf8Path(text.toUtf8().toStdString());
#endif
}
class Combo : public QComboBox {
    void wheelEvent(QWheelEvent *e) override {
        if (hasFocus())
            QComboBox::wheelEvent(e);
        else
            e->ignore();
    }
};
class Spin : public QDoubleSpinBox {
    void wheelEvent(QWheelEvent *e) override {
        if (hasFocus())
            QDoubleSpinBox::wheelEvent(e);
        else
            e->ignore();
    }
};
} // namespace
ExportDialog::ExportDialog(std::filesystem::path root, std::shared_ptr<const Session> session,
                           QWidget *parent)
    : QDialog(parent), session_(std::move(session)) {
    setObjectName("exportDialog");
    setWindowTitle(tr("Export audio"));
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *body = new QWidget;
    auto *content = new QVBoxLayout(body);
    auto *form = new QFormLayout;
    track_ = new Combo;
    track_->setObjectName("exportTrack");
    track_->setFocusPolicy(Qt::StrongFocus);
    for (const auto &track : session_->tracks)
        track_->addItem(
            QString::fromUtf8(track.name.data(), static_cast<qsizetype>(track.name.size())),
            QString::fromStdString(track.id.str()));
    start_ = new QLineEdit;
    end_ = new QLineEdit;
    destination_ = new QLineEdit(
        text(root.parent_path() / (root.filename().native() + path("-mix.wav").native())));
    start_->setObjectName("exportStartFrame");
    end_->setObjectName("exportEndFrame");
    destination_->setObjectName("exportDestination");
    start_->setMaxLength(40);
    end_->setMaxLength(40);
    form->addRow(tr("Audio track"), track_);
    form->addRow(tr("Start frame"), start_);
    form->addRow(tr("End frame (exclusive)"), end_);
    auto *browseRow = new QHBoxLayout;
    browseRow->addWidget(destination_, 1);
    auto *browse = new QPushButton(tr("Browse…"));
    browse->setObjectName("exportBrowse");
    browseRow->addWidget(browse);
    form->addRow(tr("WAV destination"), browseRow);
    tail_ = new QCheckBox(tr("Include the EQ tail"));
    tail_->setObjectName("exportIncludeTail");
    tailLimit_ = new Spin;
    tailLimit_->setObjectName("exportTailLimit");
    tailLimit_->setRange(.1, 60);
    tailLimit_->setDecimals(2);
    tailLimit_->setValue(10);
    tailLimit_->setSuffix(tr(" seconds"));
    tailLimit_->setEnabled(false);
    rf64_ = new QCheckBox(tr("Use RF64 for this WAV"));
    rf64_->setObjectName("exportForceRf64");
    form->addRow(tail_);
    form->addRow(tr("Maximum tail"), tailLimit_);
    form->addRow(rf64_);
    content->addLayout(form);
    duration_ = new QLabel;
    duration_->setObjectName("exportDuration");
    duration_->setWordWrap(true);
    duration_->setTextFormat(Qt::PlainText);
    content->addWidget(duration_);
    auto *notice =
        new QLabel(tr("Float32 WAV keeps levels above 0 dBFS. Large files use RF64 automatically. "
                      "The export captures accepted project settings when you start; later edits "
                      "do not change it. "
                      "Existing files require a separate replacement confirmation."));
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText);
    content->addWidget(notice);
    validation_ = new QLabel;
    validation_->setObjectName("exportValidation");
    validation_->setTextFormat(Qt::PlainText);
    validation_->setWordWrap(true);
    content->addWidget(validation_);
    content->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    export_ = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    export_->setObjectName("startExportJob");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto selected = QFileDialog::getSaveFileName(
            this, tr("Export WAV"), destination_->text(), tr("WAV audio (*.wav)"), nullptr,
            QFileDialog::DontConfirmOverwrite);
        if (!selected.isEmpty())
            destination_->setText(selected);
    });
    connect(track_, &QComboBox::currentIndexChanged, this, [this] { setRange(); });
    for (auto *field : {start_, end_, destination_})
        connect(field, &QLineEdit::textChanged, this, [this] { validateFields(); });
    connect(tail_, &QCheckBox::toggled, this, [this](bool value) {
        tailLimit_->setEnabled(value);
        validateFields();
    });
    connect(tailLimit_, &QDoubleSpinBox::valueChanged, this, [this] { validateFields(); });
    setRange();
    const auto available = screen()->availableGeometry();
    resize(std::min(740, available.width() - 32), std::min(470, available.height() - 32));
}
void ExportDialog::setRange() {
    Frame begin = session_->exportStartFrame, end = session_->exportEndFrame;
    if (end <= begin && track_->currentIndex() >= 0) {
        begin = 0;
        end = 0;
        const auto &track = session_->tracks[static_cast<std::size_t>(track_->currentIndex())];
        for (const auto &clip : track.clips)
            end = std::max(end, clip.startFrame + clip.lengthFrames);
    }
    start_->setText(QLocale().toString(static_cast<qlonglong>(begin)));
    end_->setText(QLocale().toString(static_cast<qlonglong>(end)));
    validateFields();
}
std::optional<ExportSelection> ExportDialog::fields() const {
    if (track_->currentIndex() < 0 || destination_->text().trimmed().isEmpty() ||
        destination_->text().contains(QChar(0)))
        return {};
    bool a = false, b = false;
    const Frame start = QLocale().toLongLong(start_->text().trimmed(), &a);
    const Frame end = QLocale().toLongLong(end_->text().trimmed(), &b);
    if (!a || !b || start < 0 || end <= start)
        return {};
    ExportSelection selection{ExportSpec(Id(track_->currentData().toString().toStdString()))};
    selection.spec.startFrame = start;
    selection.spec.endFrame = end;
    selection.spec.tail = tail_->isChecked() ? ExportTail::UntilSilent : ExportTail::ExactRange;
    selection.spec.maximumTailFrames =
        static_cast<Frame>(tailLimit_->value() * session_->sampleRate);
    selection.spec.silentWindowFrames = (session_->sampleRate + 9) / 10;
    selection.spec.maximumProcessFrames = Frame(session_->sampleRate) * 60 * 60 * 24;
    selection.spec.forceRf64 = rf64_->isChecked();
    selection.destination =
        path(destination_->text()); // File/path validation belongs to the worker.
    return selection;
}
void ExportDialog::validateFields() {
    const auto values = fields();
    export_->setEnabled(values.has_value());
    validation_->setText(
        values ? QString() : tr("Choose a destination and a range with end greater than start."));
    duration_->setText(
        values
            ? tr("%1 Hz · %2 seconds selected")
                  .arg(QLocale().toString(session_->sampleRate),
                       QLocale().toString(double(values->spec.endFrame - values->spec.startFrame) /
                                              session_->sampleRate,
                                          'f', 3))
            : QString());
}
void ExportDialog::accept() {
    selection_ = fields();
    if (selection_)
        QDialog::accept();
    else
        validateFields();
}
} // namespace soundcurrent::daw::ui
