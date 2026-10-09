// SPDX-License-Identifier: GPL-3.0-only
#include "import_inspection_dialog.hpp"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace soundcurrent::daw::ui {
QVariant ImportPreviewModel::data(const QModelIndex &index,int role) const {
    if (!report_ || !index.isValid() || index.row()<0 || std::size_t(index.row())>=report_->nodes().size()) return {};
    const auto &node=report_->nodes()[std::size_t(index.row())];
    if (role==Qt::ToolTipRole)
        return tr("Original file bytes: %1 + %2").arg(QLocale().toString(qulonglong(node.line.begin)),
            QLocale().toString(qulonglong(node.line.length)));
    if (role!=Qt::DisplayRole) return {};
    if (index.column()==0) {
        if (!node.key.length) return tr("Blank line");
        const auto token=report_->source().substr(node.key.begin,std::min<std::size_t>(node.key.length,120));
        return QString::fromUtf8(token.data(),qsizetype(token.size()));
    }
    if (index.column()==1) {
        if (node.kind==ReaperLineKind::BlockOpen) return tr("Group");
        if (node.kind==ReaperLineKind::BlockClose) return tr("End of group");
        if (node.kind==ReaperLineKind::Blank) return tr("Blank line");
        return tr("Property");
    }
    if (index.column()==2) return QLocale().toString(qulonglong(index.row()+1));
    if (index.column()==3) return tr("Unverified");
    return {};
}
QVariant ImportPreviewModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) return {};
    switch(section) { case 0:return tr("Item"); case 1:return tr("Type"); case 2:return tr("Line"); case 3:return tr("Status"); default:return {}; }
}
void ImportPreviewModel::setReport(std::shared_ptr<const ImportInspectionReport> report) {
    beginResetModel(); report_=std::move(report); endResetModel();
}
ImportInspectionDialog::ImportInspectionDialog(QWidget *parent,InspectionOptions options)
    : QDialog(parent),controller_(std::move(options)),model_(this) {
    setObjectName(QStringLiteral("importInspectionDialog"));
    auto *layout=new QVBoxLayout(this);
    notice_=new QLabel(this); notice_->setWordWrap(true); notice_->setTextFormat(Qt::PlainText); layout->addWidget(notice_);
    file_=new QLabel(this); file_->setWordWrap(true); file_->setTextFormat(Qt::PlainText); layout->addWidget(file_);
    summary_=new QLabel(this); summary_->setObjectName(QStringLiteral("importInspectionStatus"));
    summary_->setWordWrap(true); summary_->setTextFormat(Qt::PlainText); layout->addWidget(summary_);
    auto *table=new QTableView(this); table->setObjectName(QStringLiteral("importInspectionTable"));
    table->setModel(&model_); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setResizeContentsPrecision(64);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table->verticalHeader()->hide(); layout->addWidget(table,1);
    auto *files=new QHBoxLayout;
    choose_=new QPushButton(this); choose_->setObjectName(QStringLiteral("chooseImportProject")); files->addWidget(choose_);
    open_=new QPushButton(this); open_->setObjectName(QStringLiteral("openImportInspection")); files->addWidget(open_);
    save_=new QPushButton(this); save_->setObjectName(QStringLiteral("saveImportInspection")); files->addWidget(save_);
    layout->addLayout(files);
    auto *buttons=new QDialogButtonBox(this);
    cancel_=buttons->addButton(QString(),QDialogButtonBox::ActionRole); cancel_->setObjectName(QStringLiteral("cancelImportInspection"));
    close_=buttons->addButton(QDialogButtonBox::Close); layout->addWidget(buttons);
    connect(choose_,&QPushButton::clicked,this,[this] {
        const auto chosen=QFileDialog::getOpenFileName(this,tr("Choose a REAPER project"),{},tr("REAPER projects (*.rpp)"));
        if (!chosen.isEmpty()) {
#ifdef _WIN32
            inspect(std::filesystem::path(chosen.toStdWString()));
#else
            inspect(utf8Path(chosen.toUtf8().toStdString()));
#endif
        }
    });
    connect(open_,&QPushButton::clicked,this,[this] {
        const auto chosen=QFileDialog::getOpenFileName(this,tr("Open saved inspection"),{},tr("SoundCurrent inspections (*.scinspect)"));
        if (!chosen.isEmpty()) {
#ifdef _WIN32
            openInspection(std::filesystem::path(chosen.toStdWString()));
#else
            openInspection(utf8Path(chosen.toUtf8().toStdString()));
#endif
        }
    });
    connect(save_,&QPushButton::clicked,this,[this] {
        auto chosen=QFileDialog::getSaveFileName(this,tr("Save inspection to a new file"),{},
            tr("SoundCurrent inspections (*.scinspect)"),nullptr,QFileDialog::DontConfirmOverwrite);
        if (!chosen.isEmpty()) {
            if (!chosen.endsWith(QStringLiteral(".scinspect"),Qt::CaseInsensitive)) chosen+=QStringLiteral(".scinspect");
#ifdef _WIN32
            saveInspection(std::filesystem::path(chosen.toStdWString()));
#else
            saveInspection(utf8Path(chosen.toUtf8().toStdString()));
#endif
        }
    });
    connect(cancel_,&QPushButton::clicked,this,[this]{controller_.requestCancel();});
    connect(close_,&QPushButton::clicked,this,&QWidget::close);
    timer_=new QTimer(this); timer_->setInterval(20); connect(timer_,&QTimer::timeout,this,&ImportInspectionDialog::poll);
    timer_->start(); retranslate();
    const auto available=screen()->availableGeometry(); resize(std::min(740,available.width()),std::min(460,available.height()));
    poll();
}
bool ImportInspectionDialog::inspect(const std::filesystem::path &path) {
    if (closing_) return false;
    const bool accepted=controller_.submit(path)==Admission::Accepted;
    if (accepted) { shown_.reset(); model_.setReport({}); poll(); }
    return accepted;
}
bool ImportInspectionDialog::openInspection(const std::filesystem::path &path) {
    if (closing_) return false;
    const bool accepted=controller_.openBundle(path)==Admission::Accepted;
    if (accepted) { shown_.reset(); model_.setReport({}); poll(); }
    return accepted;
}
bool ImportInspectionDialog::saveInspection(const std::filesystem::path &path) {
    if (closing_) return false;
    const auto report=controller_.snapshot()->report;
    if (!report) return false;
    const bool accepted=controller_.saveBundle(path,report)==Admission::Accepted;
    if (accepted) poll();
    return accepted;
}
void ImportInspectionDialog::retranslate() {
    setWindowTitle(tr("Inspect foreign project")); choose_->setText(tr("Choose project…"));
    open_->setText(tr("Open inspection…")); save_->setText(tr("Save inspection…"));
    cancel_->setText(tr("Cancel")); close_->setText(tr("Close"));
    notice_->setText(tr("REAPER project inspection preview. Conversion is not available yet. "
                       "The original file and current project remain unchanged."));
    if (shown_) { model_.setReport({}); model_.setReport(shown_); }
}
void ImportInspectionDialog::poll() {
    const auto view=controller_.snapshot();
    choose_->setEnabled(!view->busy && !closing_); cancel_->setEnabled(view->busy && !closing_);
    open_->setEnabled(!view->busy && !closing_);
    save_->setEnabled(bool(view->report) && !view->busy && !closing_);
    const auto path=view->path.u8string(); file_->setText(QString::fromUtf8(reinterpret_cast<const char *>(path.data()),qsizetype(path.size())));
    if (view->report!=shown_) { shown_=view->report; model_.setReport(shown_); }
    if (closing_) summary_->setText(tr("Stopping inspection…"));
    else if (view->busy) {
        if (view->operation==InspectionOperation::SaveBundle) summary_->setText(tr("Saving inspection…"));
        else if (view->operation==InspectionOperation::OpenBundle) summary_->setText(tr("Opening inspection…"));
        else summary_->setText(tr("Inspecting project…"));
    }
    else if (view->phase==InspectionPhase::Complete && view->operation==InspectionOperation::SaveBundle)
        summary_->setText(tr("Inspection saved with the original source bytes. Project properties remain unverified."));
    else if (view->phase==InspectionPhase::Complete) summary_->setText(
        tr("%n source line(s) inspected. Project properties are unverified.",nullptr,int(view->report->nodes().size())));
    else if (view->phase==InspectionPhase::Canceled) summary_->setText(tr("Inspection canceled."));
    else if (view->phase==InspectionPhase::Fault) {
        if (view->operation==InspectionOperation::SaveBundle)
            summary_->setText(tr("The inspection could not be saved. Choose a new file name in a writable folder; existing files are preserved."));
        else if (view->operation==InspectionOperation::OpenBundle)
            summary_->setText(tr("The inspection could not be opened. It may be unreadable, damaged, unsupported, or too large for the available import memory."));
        else if (view->timedOut) summary_->setText(tr("Inspection exceeded its time limit."));
        else if (view->messageId=="import.resource_limit") summary_->setText(tr("There is not enough import memory, or the file exceeds the inspection limits."));
        else if (view->messageId=="import.invalid_structure") summary_->setText(tr("This project uses invalid or unsupported syntax for this inspector."));
        else if (view->messageId=="import.io_error") summary_->setText(tr("The selected project or inspection worker could not be read."));
        else summary_->setText(tr("The inspection failed or its result could not be verified."));
    } else summary_->setText(tr("Choose a project to inspect."));
    if (closing_ && view->closed) { shown_.reset(); model_.setReport({}); close(); }
}
void ImportInspectionDialog::requestShutdown() { closing_=true; controller_.requestShutdown(); }
void ImportInspectionDialog::closeEvent(QCloseEvent *event) {
    if (retired()) { event->accept(); return; }
    event->ignore(); requestShutdown();
}
void ImportInspectionDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event); if (event->type()==QEvent::LanguageChange) { retranslate(); poll(); }
}
} // namespace soundcurrent::daw::ui
