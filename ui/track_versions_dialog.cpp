// SPDX-License-Identifier: GPL-3.0-only
#include "timeline_editor.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <limits>
namespace soundcurrent::daw::ui {
namespace {
// Immutable borrow: no per-version widget, copied name, or index allocation.
class VersionList final : public QAbstractListModel {
  public:
    VersionList(const Track &track, QString original, QObject *parent)
        : QAbstractListModel(parent), track_(track), original_(std::move(original)) {
        if (track.inactiveVersions.size() >= std::size_t(std::numeric_limits<int>::max()))
            throw ResourceLimitError("Qt version list rows", track.inactiveVersions.size(),
                                     std::numeric_limits<int>::max()-1);
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(track_.inactiveVersions.size())+1;
    }
    const TrackVersionIdentity *identity(int row) const {
        if (row < 0 || row >= rowCount()) return nullptr;
        return row == 0 ? (track_.activeVersion ? &*track_.activeVersion : nullptr)
                        : &track_.inactiveVersions[std::size_t(row-1)].identity;
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.model()!=this || index.column()!=0 ||
            index.row()<0 || index.row()>=rowCount()) return {};
        if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) return {};
        const auto *v=identity(index.row());
        return v ? QString::fromUtf8(v->name) : original_;
    }
  private:
    const Track &track_;
    QString original_;
};
}
void TimelineEditor::editTrackVersions() {
    if (!editable_ || !track()) return;
    const auto snapshot=model_; // Retain the borrowed list through modal events.
    const auto epoch=epoch_;
    const auto &t=*track(); const auto selectedTrack=t.id;
    QDialog dialog(this); dialog.setObjectName("trackVersionsDialog");
    dialog.setWindowTitle(tr("Track versions"));
    auto *body=new QVBoxLayout(&dialog);
    auto *help=new QLabel(tr("Each version keeps its own arrangement. Routing, EQ, and retained takes are shared. Stop transport before switching. Removing an inactive version can be undone."));
    help->setWordWrap(true); body->addWidget(help);
    auto *active=new QLabel(tr("Active version: %1").arg(t.activeVersion ? QString::fromUtf8(t.activeVersion->name) : tr("Original")));
    active->setTextFormat(Qt::PlainText); active->setWordWrap(true); body->addWidget(active);
    VersionList model(t,tr("Original"),&dialog);
    auto *list=new QListView; list->setObjectName("trackVersionList");
    list->setAccessibleName(tr("Track versions")); list->setUniformItemSizes(true);
    list->setModel(&model); list->setCurrentIndex(model.index(0,0)); body->addWidget(list,1);
    auto *name=new QLineEdit; name->setObjectName("trackVersionName");
    name->setAccessibleName(tr("Version name"));
    auto *label=new QLabel(tr("Version name")); label->setBuddy(name);body->addWidget(label);body->addWidget(name);
    auto *actions=new QDialogButtonBox;
    enum class Action { None, New, Duplicate, Switch, Rename, Remove };
    auto chosen=Action::None;
    const auto action=[&](const QString &text,const char *object,Action kind) {
        auto *b=actions->addButton(text,QDialogButtonBox::ActionRole);b->setObjectName(object);
        connect(b,&QPushButton::clicked,&dialog,[&,kind]{chosen=kind;dialog.accept();});return b;
    };
    auto *create=action(tr("New empty"),"newTrackVersion",Action::New);
    auto *duplicate=action(tr("Duplicate active"),"duplicateTrackVersion",Action::Duplicate);
    auto *switchTo=action(tr("Activate selected"),"switchTrackVersion",Action::Switch);
    auto *rename=action(tr("Rename selected"),"renameTrackVersion",Action::Rename);
    auto *remove=action(tr("Remove selected"),"removeTrackVersion",Action::Remove);
    actions->setOrientation(Qt::Vertical); body->addWidget(actions);
    auto *cancel=new QDialogButtonBox(QDialogButtonBox::Cancel);body->addWidget(cancel);
    connect(cancel,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const auto refresh=[&] {
        const int row=list->currentIndex().row();const bool valid=row>=0 && row<model.rowCount();
        const bool named=!name->text().trimmed().isEmpty();
        create->setEnabled(named);duplicate->setEnabled(named);
        switchTo->setEnabled(valid && row>0);remove->setEnabled(valid && row>0);
        rename->setEnabled(named && model.identity(row));
    };
    connect(name,&QLineEdit::textChanged,&dialog,refresh);
    connect(list->selectionModel(),&QItemSelectionModel::currentChanged,&dialog,[&] {
        const auto *v=model.identity(list->currentIndex().row());
        name->setText(v ? QString::fromUtf8(v->name) : tr("Original"));refresh();
    });
    name->setText(t.activeVersion ? QString::fromUtf8(t.activeVersion->name) : tr("Original"));refresh();
    dialog.resize(500,580);
    if (const auto *screen=dialog.screen()) dialog.resize(dialog.size().boundedTo(screen->availableGeometry().size()));
    if (dialog.exec()!=QDialog::Accepted || chosen==Action::None) return;
    if (!editable_ || model_!=snapshot || epoch_!=epoch || track_!=selectedTrack)
        throw ProjectError(ErrorCode::InvalidState,tr("The project changed while track versions were open. Please reopen the dialog.").toUtf8().toStdString());
    const auto value=name->text().trimmed().toUtf8().toStdString();
    const auto *selected=model.identity(list->currentIndex().row());
    if(chosen==Action::New || chosen==Action::Duplicate)
        mutate({CreateTrackVersion{selectedTrack,value,tr("Original").toUtf8().toStdString(),chosen==Action::Duplicate}});
    else if(selected) {
        if(chosen==Action::Switch)mutate({SwitchTrackVersion{selectedTrack,selected->id}});
        else if(chosen==Action::Rename)mutate({RenameTrackVersion{selectedTrack,selected->id,value}});
        else if(chosen==Action::Remove)mutate({RemoveTrackVersion{selectedTrack,selected->id}});
    }
}
}
