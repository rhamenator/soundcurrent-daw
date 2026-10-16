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
class VersionTrackList final : public QAbstractListModel {
    Q_DECLARE_TR_FUNCTIONS(VersionTrackList)
  public:
    VersionTrackList(const Session &s,QObject *parent):QAbstractListModel(parent),session_(s) {
        if(s.tracks.size()>std::size_t(std::numeric_limits<int>::max()))
            throw ResourceLimitError("Qt version track rows",s.tracks.size(),std::numeric_limits<int>::max());
    }
    int rowCount(const QModelIndex &parent={}) const override {return parent.isValid()?0:int(session_.tracks.size());}
    QVariant data(const QModelIndex &index,int role) const override {
        if(!index.isValid() || index.model()!=this || index.column()!=0 || index.row()<0 || index.row()>=rowCount())return {};
        if(role!=Qt::DisplayRole && role!=Qt::AccessibleTextRole && role!=Qt::ToolTipRole)return {};
        const auto &t=session_.tracks[std::size_t(index.row())];
        const auto caption=tr("%1 — active: %2").arg(QString::fromUtf8(t.name),
            t.activeVersion?QString::fromUtf8(t.activeVersion->name):tr("Original"));
        // Qt tooltips accept rich text; project-controlled names must stay text.
        return role==Qt::ToolTipRole?QString("<qt>%1</qt>").arg(caption.toHtmlEscaped()):caption;
    }
  private:
    const Session &session_;
};
}
void TimelineEditor::editTrackVersions() {
    if (!editable_ || !track()) return;
    const auto snapshot=model_; // Retain the borrowed list through modal events.
    const auto epoch=epoch_;
    const auto &t=*track(); const auto selectedTrack=t.id;
    QDialog dialog(this); dialog.setObjectName("trackVersionsDialog");
    dialog.setWindowTitle(tr("Track versions"));
    // Bound selection ranges, staging identities and Qt index projections for the
    // whole track list before any multi-track selection can allocate them.
    PayloadCharge selectionCharge("Version dialog track selection",std::numeric_limits<std::size_t>::max());
    selectionCharge.add(snapshot->tracks.size(),512);
    auto selectionReservation=versionMemory_.reserve(selectionCharge.bytes());
    auto *body=new QVBoxLayout(&dialog);
    auto *help=new QLabel(tr("Each version keeps its own arrangement. Routing, EQ, and retained takes are shared. Stop transport before switching. Removing an inactive version can be undone."));
    help->setWordWrap(true); body->addWidget(help);
    auto *active=new QLabel(tr("Active version: %1").arg(t.activeVersion ? QString::fromUtf8(t.activeVersion->name) : tr("Original")));
    active->setTextFormat(Qt::PlainText); active->setWordWrap(true); body->addWidget(active);
    VersionList model(t,tr("Original"),&dialog);
    auto *list=new QListView; list->setObjectName("trackVersionList");
    list->setAccessibleName(tr("Track versions")); list->setUniformItemSizes(true);
    list->setModel(&model); list->setCurrentIndex(model.index(0,0));
    VersionTrackList trackModel(*snapshot,&dialog);
    auto *tracks=new QListView;tracks->setObjectName("versionTrackSelection");
    tracks->setAccessibleName(tr("Tracks to change"));tracks->setUniformItemSizes(true);
    tracks->setSelectionMode(QAbstractItemView::MultiSelection);tracks->setModel(&trackModel);
    auto *lists=new QHBoxLayout;auto *versionsColumn=new QVBoxLayout;auto *tracksColumn=new QVBoxLayout;
    versionsColumn->addWidget(new QLabel(tr("Versions on the current track")));versionsColumn->addWidget(list);
    tracksColumn->addWidget(new QLabel(tr("Tracks to change")));tracksColumn->addWidget(tracks);
    lists->addLayout(versionsColumn,1);lists->addLayout(tracksColumn,1);body->addLayout(lists,1);
    int primaryRow=0;
    for(std::size_t n=0;n<snapshot->tracks.size();++n)if(snapshot->tracks[n].id==selectedTrack)primaryRow=int(n);
    tracks->selectionModel()->select(trackModel.index(primaryRow,0),QItemSelectionModel::Select);
    auto *selectionStatus=new QLabel;selectionStatus->setWordWrap(true);selectionStatus->setObjectName("versionSelectionStatus");
    body->addWidget(selectionStatus);
    auto *name=new QLineEdit; name->setObjectName("trackVersionName");
    name->setAccessibleName(tr("Version name"));
    auto *label=new QLabel(tr("Version name")); label->setBuddy(name);body->addWidget(label);body->addWidget(name);
    auto *actions=new QDialogButtonBox;
    enum class Action { None, New, Duplicate, Switch, Rename, Remove, Link };
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
    auto *link=action(tr("Link active versions"),"linkActiveVersions",Action::Link);
    auto *matching=actions->addButton(tr("Select matching tracks"),QDialogButtonBox::ActionRole);
    matching->setObjectName("selectMatchingVersionTracks");
    actions->setOrientation(Qt::Vertical); body->addWidget(actions);
    auto *cancel=new QDialogButtonBox(QDialogButtonBox::Cancel);body->addWidget(cancel);
    connect(cancel,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const auto selectedRows=[&] {return tracks->selectionModel()->selectedRows();};
    const auto matchingIdentity=[](const Track &candidate,const Id &group)->const TrackVersionIdentity * {
        if(candidate.activeVersion && candidate.activeVersion->groupId==group)return &*candidate.activeVersion;
        for(const auto &v:candidate.inactiveVersions)if(v.identity.groupId==group)return &v.identity;
        return nullptr;
    };
    const auto refresh=[&] {
        const int row=list->currentIndex().row();const bool valid=row>=0 && row<model.rowCount();
        const bool named=!name->text().trimmed().isEmpty();
        const auto rows=selectedRows();const auto *version=model.identity(row);
        bool allMatch=true, allInactive=true, anyInactive=false;
        for(const auto &index:rows) {
            const auto &candidate=snapshot->tracks[std::size_t(index.row())];
            const auto *peer=version && version->groupId?matchingIdentity(candidate,*version->groupId):nullptr;
            allMatch=allMatch && peer;
            const bool inactive=peer && (!candidate.activeVersion || candidate.activeVersion->id!=peer->id);
            allInactive=allInactive && inactive;anyInactive=anyInactive || inactive;
        }
        const bool group=rows.size()>1;
        create->setEnabled(named);duplicate->setEnabled(named);link->setEnabled(group);
        switchTo->setEnabled(valid && (group?(allMatch && anyInactive):row>0));
        remove->setEnabled(valid && (group?(allMatch && allInactive):row>0));
        rename->setEnabled(named && version && (!group || allMatch));
        matching->setEnabled(version && version->groupId);
        selectionStatus->setText(group && !allMatch ? tr("Some selected tracks have no matching version. Create versions together, select matching tracks, or link their active versions.") :
            tr("Selected tracks: %L1. The current track stays selected.").arg(rows.size()));
    };
    connect(tracks->selectionModel(),&QItemSelectionModel::selectionChanged,&dialog,[&] {
        const auto primary=trackModel.index(primaryRow,0);
        if(!tracks->selectionModel()->isSelected(primary))tracks->selectionModel()->select(primary,QItemSelectionModel::Select);
        refresh();
    });
    connect(matching,&QPushButton::clicked,&dialog,[&] {
        const auto *version=model.identity(list->currentIndex().row());if(!version || !version->groupId)return;
        QItemSelection selection;
        for(int row=0;row<trackModel.rowCount();++row)if(matchingIdentity(snapshot->tracks[std::size_t(row)],*version->groupId))
            selection.select(trackModel.index(row,0),trackModel.index(row,0));
        tracks->selectionModel()->select(selection,QItemSelectionModel::ClearAndSelect);
    });
    connect(name,&QLineEdit::textChanged,&dialog,refresh);
    connect(list->selectionModel(),&QItemSelectionModel::currentChanged,&dialog,[&] {
        const auto *v=model.identity(list->currentIndex().row());
        name->setText(v ? QString::fromUtf8(v->name) : tr("Original"));refresh();
    });
    name->setText(t.activeVersion ? QString::fromUtf8(t.activeVersion->name) : tr("Original"));refresh();
    dialog.resize(680,680);
    if (const auto *screen=dialog.screen()) dialog.resize(dialog.size().boundedTo(screen->availableGeometry().size()));
    if (dialog.exec()!=QDialog::Accepted || chosen==Action::None) return;
    if (!editable_ || model_!=snapshot || epoch_!=epoch || track_!=selectedTrack)
        throw ProjectError(ErrorCode::InvalidState,tr("The project changed while track versions were open. Please reopen the dialog.").toUtf8().toStdString());
    const auto value=name->text().trimmed().toUtf8().toStdString();
    const auto *selected=model.identity(list->currentIndex().row());
    const auto rows=selectedRows();std::vector<Id> selectedTracks;selectedTracks.reserve(std::size_t(rows.size()));
    for(const auto &index:rows)selectedTracks.push_back(snapshot->tracks[std::size_t(index.row())].id);
    const auto originalName=tr("Original").toUtf8().toStdString();
    if(chosen==Action::Link)mutate({LinkActiveTrackVersions{std::move(selectedTracks),originalName}});
    else if(chosen==Action::New || chosen==Action::Duplicate) {
        if(selectedTracks.size()>1)mutate({CreateTrackVersions{std::move(selectedTracks),value,originalName,chosen==Action::Duplicate}});
        else mutate({CreateTrackVersion{selectedTrack,value,originalName,chosen==Action::Duplicate}});
    } else if(selected) {
        if(selectedTracks.size()>1) {
            if(!selected->groupId)throw ProjectError(ErrorCode::InvalidState,"Selected versions have no shared group");
            const auto action=chosen==Action::Switch?TrackVersionGroupAction::Activate:
                chosen==Action::Rename?TrackVersionGroupAction::Rename:TrackVersionGroupAction::Remove;
            mutate({EditTrackVersionGroup{std::move(selectedTracks),*selected->groupId,action,value}});
        } else {
            if(chosen==Action::Switch)mutate({SwitchTrackVersion{selectedTrack,selected->id}});
            else if(chosen==Action::Rename)mutate({RenameTrackVersion{selectedTrack,selected->id,value}});
            else if(chosen==Action::Remove)mutate({RemoveTrackVersion{selectedTrack,selected->id}});
        }
    }
}
}
