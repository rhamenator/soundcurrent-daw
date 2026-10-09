// SPDX-License-Identifier: GPL-3.0-only
#include "import_media_dialog.hpp"
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QItemSelectionModel>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
namespace soundcurrent::daw::ui {
namespace {
QString literal(std::string_view bytes) {return QString::fromUtf8(bytes.data(),static_cast<qsizetype>(std::min<std::size_t>(bytes.size(),256)))+(bytes.size()>256 ? QStringLiteral("…") : QString());}
std::filesystem::path path(const QString &s) {
#ifdef _WIN32
    return std::filesystem::path(s.toStdWString());
#else
    return utf8Path(s.toUtf8().toStdString());
#endif
}
QString qpath(const std::filesystem::path &p) {const auto b=p.u8string();return QString::fromUtf8(reinterpret_cast<const char *>(b.data()),static_cast<qsizetype>(b.size()));}
}
ImportMediaModel::ImportMediaModel(std::shared_ptr<const ImportInspectionReport> source,ResourceLedger memory,QObject *parent)
    :QAbstractTableModel(parent),source_(std::move(source)) {
    if (!source_ || !source_->ownedBy(memory)) throw ProjectError(ErrorCode::InvalidState,"Unadmitted source for media preview");
    const auto count=static_cast<std::size_t>(std::count_if(source_->properties().begin(),source_->properties().end(),[](const ImportProperty &p){return p.id==ImportPropertyId::SourceFile;}));
    PayloadCharge charge("Media preview rows",memory.usage().limitBytes);charge.add(count,sizeof(Row));charge.add(4096);
    rowsGrant_=memory.reserve(charge.bytes());rows_.reserve(count);
    for (std::size_t i=0;i<source_->properties().size();++i) if (source_->properties()[i].id==ImportPropertyId::SourceFile) rows_.push_back({i,{},{}});
}
const ImportProperty *ImportMediaModel::property(int row) const {
    return row<0 || static_cast<std::size_t>(row)>=rows_.size() ? nullptr : &source_->properties()[rows_[static_cast<std::size_t>(row)].property];
}
bool ImportMediaModel::canMap(int row) const {
    const auto *p=property(row);if (!p) return false;
    const auto &object=source_->objects()[p->object];
    if (object.kind!=ImportObjectKind::Source || source_->source().substr(object.sourceType.begin,object.sourceType.length)!="WAVE") return false;
    for (auto parent=object.parent;parent!=ReaperStructureNode::noParent;parent=source_->objects()[parent].parent)
        if (source_->objects()[parent].kind==ImportObjectKind::Item && !source_->objects()[parent].singleTake) return false;
    return (p->kind==ImportValueKind::Bytes && p->status==ImportEvidenceStatus::Preserved && p->reason==ImportEvidenceReason::OriginalValue) ||
           (p->kind==ImportValueKind::None && p->status==ImportEvidenceStatus::Missing && p->reason==ImportEvidenceReason::MissingProperty);
}
std::string_view ImportMediaModel::originalReference(int row) const {
    const auto *p=property(row);return !p || p->kind!=ImportValueKind::Bytes ? std::string_view{} : source_->source().substr(p->value.begin,p->value.length);
}
QVariant ImportMediaModel::data(const QModelIndex &index,int role) const {
    const auto *p=property(index.row());if (!p || !index.isValid()) return {};
    const auto &row=rows_[static_cast<std::size_t>(index.row())];const auto original=originalReference(index.row());
    if (role==Qt::ToolTipRole) {
        if (index.column()==0) return QStringLiteral("<pre>")+literal(original).toHtmlEscaped()+QStringLiteral("</pre>");
        if (index.column()==1 && row.result && row.result->selection) {
            const auto &s=*row.result->selection;
            return QStringLiteral("<pre>")+qpath(s.root).toHtmlEscaped()+QStringLiteral("\n")+
                literal(s.relative).toHtmlEscaped()+QStringLiteral("</pre>");
        }
        return QStringLiteral("<pre>")+data(index,Qt::DisplayRole).toString().toHtmlEscaped()+QStringLiteral("</pre>");
    }
    if (role!=Qt::DisplayRole) return {};
    if(index.column()==5) {
        if(!row.copied) return tr("Not copied");
        const auto &copy=*row.copied;
        if(copy.busy) return tr("Copying or recovering…");
        if(copy.phase==MediaCopyPhase::Committed) return copy.postCommitFlushFailed ? tr("Verified copy; directory durability unconfirmed") : tr("Verified copy");
        if(copy.phase==MediaCopyPhase::RecoveryRequired) return tr("Outcome uncertain; recover this operation");
        if(copy.phase==MediaCopyPhase::Planned) return tr("Planned copy; no committed receipt");
        if(copy.phase==MediaCopyPhase::NoEvidence) return tr("No intent or receipt found");
        if(copy.phase==MediaCopyPhase::Canceled) return tr("Canceled before copying");
        return tr("Copy could not start");
    }
    if (index.column()==0) return original.empty() ? tr("Reference unavailable") : literal(original);
    if (index.column()==1 && row.result && row.result->selection) {
        const auto &s=*row.result->selection;
        return s.selectedFilename ? tr("Selected replacement: %1").arg(literal(s.relative)) : tr("Selected folder: %1").arg(qpath(s.root));
    }
    if (index.column()==2) {
        if (!canMap(index.row())) return tr("Ambiguous or unsupported source; no reference selected");
        if (!row.result) return tr("Not checked");
        const auto &s=*row.result;
        if (s.busy) return tr("Checking…");
        if (s.phase==WaveCheckPhase::Complete) return tr("Checked snapshot");
        if (s.phase==WaveCheckPhase::Canceled) return tr("Canceled");
        if (s.timedOut) return tr("Check exceeded its time limit");
        if (s.error==ErrorCode::MissingMedia) return tr("File not found in the selected folder");
        if (s.error==ErrorCode::UnsupportedSchema) return tr("Audio format not supported by this check yet");
        if (s.error==ErrorCode::ResourceLimit) return tr("File or check exceeds resource limits");
        if (s.error==ErrorCode::MediaMismatch) return tr("File changed during the check");
        if (s.error==ErrorCode::InvalidParameter) return tr("Invalid audio or a reference requiring an explicit replacement");
        return tr("Check unavailable or result could not be verified");
    }
    if (row.result && row.result->report) {
        const auto &a=row.result->report->audio();
        if (index.column()==3) return tr("%1 Hz · %2 · %3-bit").arg(QLocale().toString(a.rate),tr("%n channel(s)",nullptr,static_cast<int>(a.channels)),QLocale().toString(a.bitsPerSample));
        if (index.column()==4) return a.peak==0 ? tr("Silent") : tr("%1 dBFS").arg(QLocale().toString(20*std::log10(a.peak),'f',2));
    }
    return {};
}
QVariant ImportMediaModel::headerData(int column,Qt::Orientation orientation,int role) const {
    if (role!=Qt::DisplayRole || orientation!=Qt::Horizontal) return {};
    switch (column) {case 0:return tr("Original reference");case 1:return tr("Local media choice");case 2:return tr("Status");case 3:return tr("Audio format");case 4:return tr("Sample peak");case 5:return tr("Copy state");default:return {};}
}
void ImportMediaModel::setResult(int row,std::shared_ptr<const WaveCheckSnapshot> result) {
    if (!property(row)) return;
    auto &entry=rows_[static_cast<std::size_t>(row)];auto &saved=entry.result;if (saved==result) return;
    if(saved && result && saved->job!=result->job) entry.copied.reset();
    saved=std::move(result);emit dataChanged(index(row,0),index(row,5));
}
void ImportMediaModel::setCopyResult(int row,std::shared_ptr<const MediaCopySnapshot> result) {
    if(!property(row)) return;
    auto &saved=rows_[static_cast<std::size_t>(row)].copied;if(saved==result) return;
    saved=std::move(result);emit dataChanged(index(row,5),index(row,5));
}
std::optional<std::size_t> ImportMediaModel::propertyOrdinal(int row) const {return property(row) ? std::optional(rows_[static_cast<std::size_t>(row)].property) : std::nullopt;}
std::shared_ptr<const WaveCheckSnapshot> ImportMediaModel::checked(int row) const {return property(row) ? rows_[static_cast<std::size_t>(row)].result : nullptr;}
void ImportMediaModel::clearChecks() {for (auto &r:rows_) {r.result.reset();r.copied.reset();}retranslate();}
void ImportMediaModel::retranslate() {if (!rows_.empty()) emit dataChanged(index(0,0),index(rowCount()-1,5));emit headerDataChanged(Qt::Horizontal,0,5);}
ImportMediaDialog::ImportMediaDialog(QWidget *parent,std::shared_ptr<const ImportInspectionReport> report,WaveCheckOptions options,MediaCopyOptions copyOptions)
    :QDialog(parent),controller_(options),copier_([&]{copyOptions.memory=options.memory;return copyOptions;}()),model_(std::move(report),options.memory,this) {
    setObjectName(QStringLiteral("importMediaDialog"));setWindowModality(Qt::WindowModal);
    auto *layout=new QVBoxLayout(this);
    notice_=new QLabel(this);notice_->setWordWrap(true);notice_->setTextFormat(Qt::PlainText);layout->addWidget(notice_);
    folder_=new QLabel(this);folder_->setWordWrap(true);folder_->setTextFormat(Qt::PlainText);layout->addWidget(folder_);
    auto *limits=new QHBoxLayout;limitLabel_=new QLabel(this);limits->addWidget(limitLabel_);
    limit_=new QSpinBox(this);limit_->setObjectName(QStringLiteral("mediaCheckSizeLimit"));limit_->setRange(1,8192);limit_->setValue(static_cast<int>(std::clamp<std::uint64_t>(options.maximumSourceBytes/(1024*1024),1,8192)));limits->addWidget(limit_);limits->addStretch();layout->addLayout(limits);
    table_=new QTableView(this);table_->setObjectName(QStringLiteral("importMediaTable"));table_->setModel(&model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    table_->setColumnWidth(0,170);table_->setColumnWidth(1,170);table_->setColumnWidth(3,220);table_->setColumnWidth(4,110);
    table_->verticalHeader()->hide();layout->addWidget(table_,1);
    auto *choices=new QGridLayout;chooseRoot_=new QPushButton(this);chooseRoot_->setObjectName(QStringLiteral("chooseMediaRoot"));choices->addWidget(chooseRoot_,0,0);
    check_=new QPushButton(this);check_->setObjectName(QStringLiteral("checkSelectedMedia"));choices->addWidget(check_,0,1);
    replace_=new QPushButton(this);replace_->setObjectName(QStringLiteral("chooseMediaReplacement"));choices->addWidget(replace_,1,0);
    clear_=new QPushButton(this);clear_->setObjectName(QStringLiteral("clearMediaChecks"));choices->addWidget(clear_,1,1);layout->addLayout(choices);
    copy_=new QPushButton(this);copy_->setObjectName(QStringLiteral("copyCheckedMedia"));choices->addWidget(copy_,2,0);
    recover_=new QPushButton(this);recover_->setObjectName(QStringLiteral("recoverCopiedMedia"));choices->addWidget(recover_,2,1);
    copyStatus_=new QLabel(this);copyStatus_->setObjectName(QStringLiteral("mediaCopyStatus"));copyStatus_->setWordWrap(true);copyStatus_->setTextFormat(Qt::PlainText);layout->addWidget(copyStatus_);
    status_=new QLabel(this);status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    auto *buttons=new QDialogButtonBox(this);cancel_=buttons->addButton(QString(),QDialogButtonBox::ActionRole);cancel_->setObjectName(QStringLiteral("cancelMediaCheck"));
    close_=buttons->addButton(QDialogButtonBox::Close);layout->addWidget(buttons);
    connect(chooseRoot_,&QPushButton::clicked,this,[this]{const auto chosen=QFileDialog::getExistingDirectory(this,tr("Choose the folder containing this project's media"));if (!chosen.isEmpty()) {root_=path(chosen);poll();}});
    connect(check_,&QPushButton::clicked,this,[this]{checkReference(selectedRow(),root_);});
    connect(replace_,&QPushButton::clicked,this,[this]{const auto chosen=QFileDialog::getOpenFileName(this,tr("Choose a replacement audio file"),{},tr("WAVE audio (*.wav);;All files (*)"));if (!chosen.isEmpty()) checkReplacement(selectedRow(),path(chosen));});
    connect(copy_,&QPushButton::clicked,this,[this]{const auto chosen=QFileDialog::getExistingDirectory(this,tr("Choose a destination for the verified media copy"));if(!chosen.isEmpty()) copyChecked(selectedRow(),path(chosen));});
    connect(recover_,&QPushButton::clicked,this,[this]{
        if(copier_.snapshot()->phase==MediaCopyPhase::RecoveryRequired) {recoverLastCopy();return;}
        const auto chosen=QFileDialog::getExistingDirectory(this,tr("Choose the copied operation's UUID folder"));if(!chosen.isEmpty()) recoverCopied(path(chosen));
    });
    connect(clear_,&QPushButton::clicked,this,[this]{if (!copier_.snapshot()->busy && controller_.clearResult() && copier_.clearResult()) {model_.clearChecks();root_.clear();activeRow_=-1;activeCopyRow_=-1;admissionFailed_=false;poll();}});
    connect(cancel_,&QPushButton::clicked,this,[this]{controller_.requestCancel();copier_.requestCancel();});connect(close_,&QPushButton::clicked,this,&QWidget::close);
    connect(table_->selectionModel(),&QItemSelectionModel::selectionChanged,this,[this]{poll();});
    timer_=new QTimer(this);timer_->setInterval(20);connect(timer_,&QTimer::timeout,this,&ImportMediaDialog::poll);timer_->start();
    retranslate();const auto available=screen()->availableGeometry();resize(std::min(1000,available.width()),std::min(500,available.height()));poll();
}
int ImportMediaDialog::selectedRow() const {const auto selected=table_->selectionModel()->selectedRows();return selected.size()==1 ? selected.front().row() : -1;}
bool ImportMediaDialog::checkReference(int row,const std::filesystem::path &root) {
    if (closing_ || copier_.snapshot()->busy || !model_.canMap(row) || model_.originalReference(row).empty()) return false;
    try {
        if (controller_.submit(root,std::string(model_.originalReference(row)),static_cast<std::uint64_t>(limit_->value())*1024*1024)!=Admission::Accepted) return false;
        root_=root;activeRow_=row;admissionFailed_=false;poll();return true;
    } catch (const ProjectError &) {admissionFailed_=true;poll();return false;}
}
bool ImportMediaDialog::checkReplacement(int row,const std::filesystem::path &file) {
    if (closing_ || copier_.snapshot()->busy || !model_.canMap(row)) return false;
    try {
        if (controller_.submitReplacement(file,static_cast<std::uint64_t>(limit_->value())*1024*1024)!=Admission::Accepted) return false;
        activeRow_=row;admissionFailed_=false;poll();return true;
    } catch (const ProjectError &) {admissionFailed_=true;poll();return false;}
}
bool ImportMediaDialog::copyChecked(int row,const std::filesystem::path &destination) {
    const auto ordinal=model_.propertyOrdinal(row);const auto checked=model_.checked(row);
    if(closing_ || controller_.snapshot()->busy || !ordinal || !model_.canMap(row) || !checked || checked->phase!=WaveCheckPhase::Complete) return false;
    try {
        if(copier_.copy(model_.inspection(),*ordinal,checked,destination)!=Admission::Accepted) return false;
        activeCopyRow_=row;admissionFailed_=false;poll();return true;
    } catch(const ProjectError &) {admissionFailed_=true;poll();return false;}
}
bool ImportMediaDialog::recoverCopied(const std::filesystem::path &folder) {
    if(closing_ || controller_.snapshot()->busy || copier_.snapshot()->busy) return false;
    try {
        const auto leaf=folder.filename().u8string();Id operation(std::string(reinterpret_cast<const char *>(leaf.data()),leaf.size()));
        if(copier_.recover(folder.parent_path(),operation,static_cast<std::uint64_t>(limit_->value())*1024*1024)!=Admission::Accepted) return false;
        activeCopyRow_=-1;admissionFailed_=false;poll();return true;
    } catch(const ProjectError &) {admissionFailed_=true;poll();return false;}
}
bool ImportMediaDialog::recoverLastCopy() {
    const auto previous=copier_.snapshot();
    if(closing_ || controller_.snapshot()->busy || previous->busy || !previous->selection) return false;
    try {
        if(copier_.recover(previous->selection->destination,previous->selection->operation,previous->selection->maximumBytes)!=Admission::Accepted) return false;
        admissionFailed_=false;poll();return true;
    } catch(const ProjectError &) {admissionFailed_=true;poll();return false;}
}
void ImportMediaDialog::poll() {
    const auto s=controller_.snapshot();const auto copied=copier_.snapshot();const bool idle=!s->busy && !copied->busy && !closing_;const int row=selectedRow();
    if (activeRow_>=0 && s->job) model_.setResult(activeRow_,s);
    if(activeCopyRow_>=0 && copied->selection) model_.setCopyResult(activeCopyRow_,copied);
    chooseRoot_->setEnabled(idle);check_->setEnabled(idle && !root_.empty() && model_.canMap(row) && !model_.originalReference(row).empty());
    replace_->setEnabled(idle && model_.canMap(row));clear_->setEnabled(idle);cancel_->setEnabled((s->busy || copied->busy) && !closing_);limit_->setEnabled(idle);
    const auto checked=model_.checked(row);copy_->setEnabled(idle && model_.canMap(row) && checked && checked->phase==WaveCheckPhase::Complete);recover_->setEnabled(idle);
    recover_->setText(copied->phase==MediaCopyPhase::RecoveryRequired ? tr("Check last copy outcome") : tr("Recover copied media…"));
    folder_->setText(root_.empty() ? tr("No media folder selected. Opening an inspection does not grant access to its media.") : tr("Selected media folder: %1").arg(qpath(root_)));
    if(copied->selection) {
        const auto location=qpath(copied->selection->destination/utf8Path(copied->selection->operation.str()));
        if(copied->busy) copyStatus_->setText(tr("Copy or recovery in progress. Operation folder: %1").arg(location));
        else if(copied->phase==MediaCopyPhase::Committed) copyStatus_->setText((copied->postCommitFlushFailed ?
            tr("Media is committed and verified, but directory durability is unconfirmed. Folder: %1") :
            tr("Media is committed and verified. Folder: %1")).arg(location));
        else if(copied->phase==MediaCopyPhase::RecoveryRequired) copyStatus_->setText((copied->error==ErrorCode::MediaMismatch ?
            tr("The checked media or inspection changed. Check the copy outcome, then check the source again. Operation folder: %1") :
            tr("The copy outcome is uncertain. Recover this operation folder before using its media: %1")).arg(location));
        else if(copied->phase==MediaCopyPhase::Planned) copyStatus_->setText(tr("This operation has planned intent but no committed receipt. Partial data is preserved: %1").arg(location));
        else if(copied->phase==MediaCopyPhase::NoEvidence) copyStatus_->setText(tr("No intent or receipt was found. No completed copy is established: %1").arg(location));
        else copyStatus_->setText(tr("The copy did not start. Check the selected row, destination and available import memory."));
    } else copyStatus_->setText(tr("Verified copies preserve the original reference and audio bytes. They are not attached to the current project. Clearing choices leaves copied files intact."));
    if (closing_) status_->setText(tr("Stopping media jobs…"));
    else if (admissionFailed_) status_->setText(tr("The media job could not be started. Check the selected path and available import memory."));
    else if (s->busy) status_->setText(tr("Checking selected audio…"));
    else status_->setText(tr("Choose a media folder and check a reference, or explicitly choose a replacement. Choices stay local to this window."));
    if (closing_ && retired()) close();
}
void ImportMediaDialog::retranslate() {
    setWindowTitle(tr("Check and copy project media"));notice_->setText(tr("Project conversion is in development. Check media, then copy checked files to a new destination. The checked snapshot is verified again before copying. The current project stays unchanged."));
    limitLabel_->setText(tr("Maximum file size for this check (MiB):"));chooseRoot_->setText(tr("Choose media folder…"));check_->setText(tr("Check selected reference"));
    replace_->setText(tr("Choose replacement…"));clear_->setText(tr("Clear local choices"));cancel_->setText(tr("Cancel"));close_->setText(tr("Close"));model_.retranslate();
    copy_->setText(tr("Copy checked media…"));recover_->setText(tr("Recover copied media…"));
}
void ImportMediaDialog::requestShutdown() {closing_=true;controller_.requestShutdown();copier_.requestShutdown();}
void ImportMediaDialog::closeEvent(QCloseEvent *e) {if (retired()) {e->accept();return;}e->ignore();requestShutdown();}
void ImportMediaDialog::changeEvent(QEvent *e) {QDialog::changeEvent(e);if (e->type()==QEvent::LanguageChange) {retranslate();poll();}}
} // namespace soundcurrent::daw::ui
