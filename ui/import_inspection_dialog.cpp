// SPDX-License-Identifier: GPL-3.0-only
#include "import_inspection_dialog.hpp"
#include "import_media_dialog.hpp"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace soundcurrent::daw::ui {
QString ImportPropertyModel::fieldName(ImportPropertyId id) {
    switch (id) {
    case ImportPropertyId::ProjectSampleRate:return tr("Project sample rate");
    case ImportPropertyId::ProjectSampleRateEnabled:return tr("Use project sample rate");
    case ImportPropertyId::TrackIdentity:return tr("Track identity");
    case ImportPropertyId::TrackName:return tr("Track name");
    case ImportPropertyId::TrackGain:return tr("Track gain");
    case ImportPropertyId::TrackPan:return tr("Track pan");
    case ImportPropertyId::TrackChannels:return tr("Track channels");
    case ImportPropertyId::ItemIdentity:return tr("Item identity");
    case ImportPropertyId::ItemPosition:return tr("Item position");
    case ImportPropertyId::ItemLength:return tr("Item length");
    case ImportPropertyId::ItemFadeIn:return tr("Fade-in length");
    case ImportPropertyId::ItemFadeOut:return tr("Fade-out length");
    case ImportPropertyId::TakeName:return tr("Take name");
    case ImportPropertyId::ItemGain:return tr("Item gain");
    case ImportPropertyId::TakeGain:return tr("Take gain");
    case ImportPropertyId::TakePan:return tr("Take pan");
    case ImportPropertyId::TakeSourceOffset:return tr("Source offset");
    case ImportPropertyId::TakeRate:return tr("Take rate");
    case ImportPropertyId::TakePitch:return tr("Take pitch");
    case ImportPropertyId::SourceFile:return tr("Source file");
    }
    return {};
}
QString ImportPropertyModel::unit(ImportPropertyId id) {
    switch (id) {
    case ImportPropertyId::ProjectSampleRate:return tr("Hz");
    case ImportPropertyId::ProjectSampleRateEnabled:return tr("0 / 1");
    case ImportPropertyId::TrackChannels:return tr("channels");
    case ImportPropertyId::TrackGain:case ImportPropertyId::ItemGain:case ImportPropertyId::TakeGain:return tr("linear gain");
    case ImportPropertyId::TrackPan:case ImportPropertyId::TakePan:return tr("source pan");
    case ImportPropertyId::ItemPosition:case ImportPropertyId::ItemLength:case ImportPropertyId::ItemFadeIn:
    case ImportPropertyId::ItemFadeOut:case ImportPropertyId::TakeSourceOffset:return tr("seconds");
    case ImportPropertyId::TakeRate:return tr("ratio");
    case ImportPropertyId::TakePitch:return tr("semitones");
    default:return tr("original bytes");
    }
}
QString ImportPropertyModel::status(ImportEvidenceStatus value) {
    switch (value) {
    case ImportEvidenceStatus::Preserved:return tr("Value retained");
    case ImportEvidenceStatus::Converted:return tr("Converted");
    case ImportEvidenceStatus::Unsupported:return tr("Unsupported");
    case ImportEvidenceStatus::Missing:return tr("Missing");
    case ImportEvidenceStatus::Unverified:return tr("Unverified");
    }
    return {};
}
QString ImportPropertyModel::reason(ImportEvidenceReason value) {
    switch (value) {
    case ImportEvidenceReason::OriginalValue:return tr("Original value; audio import is not available yet.");
    case ImportEvidenceReason::UnknownSemantics:return tr("Original state has not been interpreted.");
    case ImportEvidenceReason::InvalidNumber:return tr("Invalid or out-of-range original number.");
    case ImportEvidenceReason::UnknownShape:return tr("Unverified original field layout.");
    case ImportEvidenceReason::InvalidToken:return tr("Malformed original token.");
    case ImportEvidenceReason::DuplicateProperty:return tr("Repeated field; no occurrence selected.");
    case ImportEvidenceReason::MissingProperty:return tr("The source project does not specify this value.");
    case ImportEvidenceReason::AmbiguousTake:return tr("Take selection is ambiguous.");
    case ImportEvidenceReason::ProcessingNotImplemented:return tr("This processing setting cannot be imported yet.");
    case ImportEvidenceReason::SourceNotImplemented:return tr("This source type is not implemented.");
    }
    return {};
}
QVariant ImportPropertyModel::data(const QModelIndex &index,int role) const {
    if (!report_ || !index.isValid() || index.row()<0 || std::size_t(index.row())>=report_->properties().size()) return {};
    const auto &p=report_->properties()[std::size_t(index.row())];
    if (role==Qt::ToolTipRole) {
        if (index.column()==5) return reason(p.reason);
        if (p.status==ImportEvidenceStatus::Missing) return reason(p.reason);
        if (index.column()==2 && p.kind==ImportValueKind::Bytes) {
            const auto bytes=report_->source().substr(p.value.begin,std::min<std::size_t>(p.value.length,256));
            // Qt tooltips recognize rich text. Escape every foreign byte-derived
            // character inside a controlled wrapper; never load embedded markup.
            return QStringLiteral("<pre>")+QString::fromUtf8(bytes.data(),qsizetype(bytes.size())).toHtmlEscaped()+QStringLiteral("</pre>");
        }
        return tr("Original file bytes: %1 + %2").arg(QLocale().toString(qulonglong(p.value.begin)),QLocale().toString(qulonglong(p.value.length)));
    }
    if (role!=Qt::DisplayRole) return {};
    switch (index.column()) {
    case 0: {
        QString kind;
        switch (report_->objects()[p.object].kind) {
        case ImportObjectKind::Project:kind=tr("Project");break;
        case ImportObjectKind::Track:kind=tr("Track");break;
        case ImportObjectKind::Item:kind=tr("Item");break;
        case ImportObjectKind::Source:kind=tr("Source");break;
        }
        return tr("%1 · %2").arg(kind,QLocale().toString(qulonglong(p.object+1)));
    }
    case 1:return fieldName(p.id);
    case 2:
        if (p.kind==ImportValueKind::Number) return QLocale().toString(p.number,'g',17);
        if (p.kind==ImportValueKind::Bytes) {
            const auto bytes=report_->source().substr(p.value.begin,std::min<std::size_t>(p.value.length,256));
            auto value=QString::fromUtf8(bytes.data(),qsizetype(bytes.size()));
            if (p.value.length>bytes.size()) value+=QStringLiteral("…");
            return value;
        }
        return tr("Unavailable");
    case 3:return unit(p.id);
    case 4:return status(p.status);
    case 5:return reason(p.reason);
    default:return {};
    }
}
QVariant ImportPropertyModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) return {};
    switch (section) {
    case 0:return tr("Source object");case 1:return tr("Property");case 2:return tr("Original value");
    case 3:return tr("Units");case 4:return tr("Status");case 5:return tr("Details");default:return {};
    }
}
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
    if (index.column()==3) {
        if (report_->hasProperties()) {
            const auto status=report_->lineEvidence()[std::size_t(index.row())].status;
            if (status==ImportEvidenceStatus::Unsupported) return tr("Unsupported");
            if (status==ImportEvidenceStatus::Preserved) return tr("Bytes retained");
        }
        return tr("Unverified");
    }
    if (index.column()==4) {
        const auto bytes=report_->source().substr(node.line.begin,std::min<std::size_t>(node.line.length,256));
        auto value=QString::fromUtf8(bytes.data(),qsizetype(bytes.size())).trimmed();
        if (node.line.length>bytes.size()) value+=QStringLiteral("…");
        return value;
    }
    return {};
}
QVariant ImportPreviewModel::headerData(int section,Qt::Orientation orientation,int role) const {
    if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) return {};
    switch(section) { case 0:return tr("Item"); case 1:return tr("Type"); case 2:return tr("Line"); case 3:return tr("Status"); case 4:return tr("Original line"); default:return {}; }
}
void ImportPreviewModel::setReport(std::shared_ptr<const ImportInspectionReport> report) {
    beginResetModel(); report_=std::move(report); endResetModel();
}
ImportInspectionDialog::ImportInspectionDialog(QWidget *parent,InspectionOptions options)
    : QDialog(parent),controller_(options),model_(this),properties_(this),mediaMemory_(options.memory) {
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
    table->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    table->verticalHeader()->hide();
    tabs_=new QTabWidget(this); tabs_->setObjectName(QStringLiteral("importInspectionTabs"));
    auto *properties=new QTableView(this); properties->setObjectName(QStringLiteral("importPropertiesTable"));
    properties->setModel(&properties_); properties->setEditTriggers(QAbstractItemView::NoEditTriggers);
    properties->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    properties->horizontalHeader()->setResizeContentsPrecision(64);
    properties->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    properties->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Interactive);
    properties->setColumnWidth(5,160); properties->verticalHeader()->hide();
    tabs_->addTab(properties,QString()); tabs_->addTab(table,QString()); layout->addWidget(tabs_,1);
    auto *files=new QGridLayout;
    choose_=new QPushButton(this); choose_->setObjectName(QStringLiteral("chooseImportProject")); files->addWidget(choose_,0,0);
    open_=new QPushButton(this); open_->setObjectName(QStringLiteral("openImportInspection")); files->addWidget(open_,0,1);
    save_=new QPushButton(this); save_->setObjectName(QStringLiteral("saveImportInspection")); files->addWidget(save_,1,0);
    mediaButton_=new QPushButton(this);mediaButton_->setObjectName(QStringLiteral("checkImportMedia"));files->addWidget(mediaButton_,1,1);
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
    connect(mediaButton_,&QPushButton::clicked,this,[this]{openMediaCheck();});
    connect(close_,&QPushButton::clicked,this,&QWidget::close);
    timer_=new QTimer(this); timer_->setInterval(20); connect(timer_,&QTimer::timeout,this,&ImportInspectionDialog::poll);
    timer_->start(); retranslate();
    const auto available=screen()->availableGeometry(); resize(std::min(740,available.width()),std::min(460,available.height()));
    poll();
}
bool ImportInspectionDialog::inspect(const std::filesystem::path &path) {
    if (closing_ || (media_ && !media_->retired())) return false;
    const bool accepted=controller_.submit(path)==Admission::Accepted;
    if (accepted) { shown_.reset(); model_.setReport({}); properties_.setReport({}); poll(); }
    return accepted;
}
bool ImportInspectionDialog::openInspection(const std::filesystem::path &path) {
    if (closing_ || (media_ && !media_->retired())) return false;
    const bool accepted=controller_.openBundle(path)==Admission::Accepted;
    if (accepted) { shown_.reset(); model_.setReport({}); properties_.setReport({}); poll(); }
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
    mediaButton_->setText(tr("Check media…"));
    cancel_->setText(tr("Cancel")); close_->setText(tr("Close"));
    notice_->setText(tr("REAPER project inspection preview. Conversion is not available yet. "
                       "The original file and current project remain unchanged."));
    tabs_->setTabText(0,tr("Properties")); tabs_->setTabText(1,tr("Original source"));
    if (shown_) { model_.setReport({}); model_.setReport(shown_); properties_.setReport({}); properties_.setReport(shown_); }
}
void ImportInspectionDialog::poll() {
    const auto view=controller_.snapshot();
    choose_->setEnabled(!view->busy && !closing_); cancel_->setEnabled(view->busy && !closing_);
    open_->setEnabled(!view->busy && !closing_);
    save_->setEnabled(bool(view->report) && !view->busy && !closing_);
    mediaButton_->setEnabled(view->report && view->report->hasProperties() && !view->busy && !closing_);
    const auto path=view->path.u8string(); file_->setText(QString::fromUtf8(reinterpret_cast<const char *>(path.data()),qsizetype(path.size())));
    if (view->report!=shown_) {
        shown_=view->report; model_.setReport(shown_); properties_.setReport(shown_);
        valueCount_=shown_ ? int(std::count_if(shown_->properties().begin(),shown_->properties().end(),
            [](const ImportProperty &p){return p.kind!=ImportValueKind::None;})) : 0;
        tabs_->setTabEnabled(0,shown_ && shown_->hasProperties());
        tabs_->setCurrentIndex(shown_ && shown_->hasProperties() ? 0 : 1);
    }
    if (closing_) summary_->setText(tr("Stopping inspection…"));
    else if (view->busy) {
        if (view->operation==InspectionOperation::SaveBundle) summary_->setText(tr("Saving inspection…"));
        else if (view->operation==InspectionOperation::OpenBundle) summary_->setText(tr("Opening inspection…"));
        else summary_->setText(tr("Inspecting project…"));
    }
    else if (view->phase==InspectionPhase::Complete && view->operation==InspectionOperation::SaveBundle)
        summary_->setText(tr("Inspection saved with the original source bytes. Project properties remain unverified."));
    else if (view->phase==InspectionPhase::Complete && view->report->hasProperties()) summary_->setText(
        tr("%n original property value(s) found. Audio import is not available yet.",nullptr,valueCount_));
    else if (view->phase==InspectionPhase::Complete) summary_->setText(
        tr("%n source line(s) inspected. This saved outline has no property preview.",nullptr,int(view->report->nodes().size())));
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
    if (closing_ && retired()) { shown_.reset(); model_.setReport({}); properties_.setReport({}); close(); }
}
ImportMediaDialog *ImportInspectionDialog::openMediaCheck() {
    const auto view=controller_.snapshot();if (closing_ || view->busy || !view->report || !view->report->hasProperties()) return nullptr;
    if (media_ && !media_->retired()) {media_->show();media_->raise();return media_;}
    try {
        delete media_;media_=nullptr;WaveCheckOptions options;options.memory=mediaMemory_;
        media_=new ImportMediaDialog(this,view->report,std::move(options));media_->show();return media_;
    } catch (const ProjectError &) {QMessageBox::warning(this,tr("Media check unavailable"),tr("There is not enough import memory to display the media checklist."));return nullptr;}
}
bool ImportInspectionDialog::retired() const {return controller_.snapshot()->closed && (!media_ || media_->retired());}
void ImportInspectionDialog::requestShutdown() { closing_=true;if (media_) media_->requestShutdown();controller_.requestShutdown(); }
void ImportInspectionDialog::closeEvent(QCloseEvent *event) {
    if (retired()) { event->accept(); return; }
    event->ignore(); requestShutdown();
}
void ImportInspectionDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event); if (event->type()==QEvent::LanguageChange) { retranslate(); poll(); }
}
} // namespace soundcurrent::daw::ui
