// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/resource_ledger.hpp>
#include "session_list_model.hpp"
#include "timeline_view.hpp"
#include <QGroupBox>
#include <QCoreApplication>
#include <functional>
#include <memory>
class QListView;

class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QSlider;
namespace soundcurrent::daw::ui {
class SessionListModel;
class TimelineView;
class TimelineEditor : public QGroupBox {
  public:
    explicit TimelineEditor(QWidget *parent = nullptr, ResourceLedger = ResourceLedger{});
    struct Prepared;
    std::shared_ptr<Prepared> prepareModel(std::shared_ptr<const Session>, std::uint64_t epoch,
                                           bool editable) const;
    void commitModel(std::shared_ptr<Prepared>);
    std::optional<Id> preparedTrack(const std::shared_ptr<Prepared> &) const;
    void editing(bool);
    std::size_t resourceBytes() const;
    std::function<bool(std::shared_ptr<const Session>, std::optional<Id>)> selectionAdmission;
    ~TimelineEditor() override;
    std::function<bool(std::vector<SessionEdit>)> submit;
    std::function<void()> selectionChanged;
    void updateModel(std::shared_ptr<const Session>, std::uint64_t epoch, bool editable);
    std::optional<Id> selectedTrack() const {
        return track_;
    }
    std::optional<Id> selectedClip() const {
        return clip_;
    }
    bool selectTrack(const Id &);
    static QString tr(const char *s) {
        return QCoreApplication::translate("TimelineEditor", s);
    }

  private:
    std::shared_ptr<Prepared> prepare(std::shared_ptr<const Session>, std::uint64_t, bool,
                                      std::optional<Id>, std::optional<Id>, bool) const;
    bool select(std::optional<Id>, std::optional<Id>);
    std::shared_ptr<const Session> model_;
    std::uint64_t epoch_ = 0;
    bool editable_ = false, refreshing_ = false;
    std::optional<Id> track_, clip_, pendingTrack_;
    QListView *tracks_;
    TimelineView *view_;
    SessionListModel *trackList_, *destinations_, *assets_, *clipList_;
    QLineEdit *name_, *start_, *source_, *length_, *split_;
    QComboBox *layout_, *destination_, *asset_, *clips_;
    QSpinBox *channels_;
    QSlider *zoom_;
    QLabel *status_;
    std::vector<QPushButton *> mutations_;
    const Track *track() const;
    const Clip *clip() const;
    void refresh(bool forceFields = false, bool redraw = true,
                 std::optional<QString> destination = {}, std::optional<QString> asset = {});
    void draw();
    void mutate(std::vector<SessionEdit>);
    void operation(const std::function<void()> &);
    Frame frame(QLineEdit *) const;
};
} // namespace soundcurrent::daw::ui
