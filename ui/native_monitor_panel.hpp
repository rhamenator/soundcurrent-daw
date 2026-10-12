// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_monitor_controller.hpp"
#include <QGroupBox>
#include <QCoreApplication>
class QPushButton;
class QLabel;
class QSpinBox;
class QCheckBox;
class QTimer;
class QCloseEvent;
namespace soundcurrent::daw::ui {
// Reusable visible transport for the owned route. Caller supplies selected device
// context from its worker inventory; this widget never queries SDK/filesystem.
class NativeMonitorPanel final : public QGroupBox {
    Q_DECLARE_TR_FUNCTIONS(NativeMonitorPanel)
  public:
    explicit NativeMonitorPanel(NativeMonitorControllerOptions = {},QWidget * = nullptr,
        ResourceLedger panelMemory = ResourceLedger{16*1024*1024,"Native recording panel"});
    ~NativeMonitorPanel();
    bool setPreparation(const NativeMonitorPreparation &); // Admitted immutable copy.
    void poll();
    void beginClose(); // Asynchronous: caller waits for closeReady before destruction.
    bool closeReady() const;
    std::shared_ptr<const NativeMonitorSnapshot> snapshot() const;
    bool parameter(const EqSettings &,const ParameterAddress &); // Existing inspector seam.
  protected:
    void closeEvent(QCloseEvent *) override;
  private:
    ResourceLedger memory_;
    ResourceLease widgetCredit_;
    NativeMonitorController controller_;
    std::shared_ptr<const NativeMonitorPreparation> preparation_;
    QPushButton *prepare_,*start_,*cancel_,*inspect_,*keep_;
    QLabel *devices_,*status_,*progress_,*receipt_;
    QSpinBox *seconds_;
    QCheckBox *eq_;
    QTimer *timer_;
    std::uint64_t nextGeneration_=0,nextRevision_=0,errorSeen_=0;
    bool closing_=false,windowClose_=false;
    QString admission_;
    bool send(NativeMonitorCommand);
    NativeMonitorCommand command(NativeMonitorCommandKind) const;
};
} // namespace soundcurrent::daw::ui
