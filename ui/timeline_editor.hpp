// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/session.hpp>
#include <QGroupBox>
#include <QCoreApplication>
#include <functional>
#include <memory>
class QListWidget;
class QGraphicsView;
class QGraphicsScene;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QSlider;
namespace soundcurrent::daw::ui {
class TimelineEditor : public QGroupBox {
  public:
    explicit TimelineEditor(QWidget *parent = nullptr);
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
    std::shared_ptr<const Session> model_;
    std::uint64_t epoch_ = 0;
    bool editable_ = false, refreshing_ = false;
    std::optional<Id> track_, clip_, pendingTrack_;
    QListWidget *tracks_;
    QGraphicsScene *scene_;
    QGraphicsView *view_;
    QLineEdit *name_, *start_, *source_, *length_, *split_;
    QComboBox *layout_, *destination_, *asset_, *clips_;
    QSpinBox *channels_;
    QSlider *zoom_;
    QLabel *status_;
    std::vector<QPushButton *> mutations_;
    const Track *track() const;
    const Clip *clip() const;
    void refresh(bool forceFields = false, bool redraw = true);
    void draw();
    void mutate(std::vector<SessionEdit>);
    void operation(const std::function<void()> &);
    Frame frame(QLineEdit *) const;
};
} // namespace soundcurrent::daw::ui
