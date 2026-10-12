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
class QComboBox;
class QStandardItemModel;
namespace soundcurrent::daw::ui {
// Reusable visible transport for the owned route. Caller supplies selected device
// context from its worker inventory; this widget never queries SDK/filesystem.
class NativeMonitorPanel final : public QGroupBox {
    Q_DECLARE_TR_FUNCTIONS(NativeMonitorPanel)
  public:
    explicit NativeMonitorPanel(NativeMonitorControllerOptions = {},QWidget * = nullptr,
        ResourceLedger panelMemory = ResourceLedger{16*1024*1024,"Native recording panel"});
    ~NativeMonitorPanel();
    bool interactiveDevices() const;
    void preparationGuard(std::function<bool(const NativeMonitorPreparation &)>);
    void clearPreparation(); // Inactive context only; releases its payload credit.
    bool exportOriginal(const std::filesystem::path &);
    bool enabled(bool);
    bool setPreparation(const NativeMonitorPreparation &); // Admitted immutable copy.
    void poll();
    void cancel(); // Urgent intent, independent of command quota.
    bool busy() const; // Includes prepared route and retained result.
    void beginClose(); // Asynchronous: caller waits for closeReady before destruction.
    bool closeReady() const;
    std::shared_ptr<const NativeMonitorSnapshot> snapshot() const;
    bool parameter(const EqSettings &,const ParameterAddress &); // Existing inspector seam.
  protected:
    void closeEvent(QCloseEvent *) override;
  private:
    ResourceLedger memory_;
    ResourceLease widgetCredit_;
    bool interactive_ = false, refreshing_ = false;
    NativeMonitorController controller_;
    std::uint64_t refreshCompleted_ = 0, refreshCancel_ = 0, shownInventoryRevision_ = 0;
    ResourceLease modelCredit_;
    std::shared_ptr<const std::vector<AudioPort>> shownPorts_;
    std::unique_ptr<QStandardItemModel> inputModel_,outputModel_;
    QComboBox *inputDevice_ = nullptr,*outputDevice_ = nullptr,*mapping_ = nullptr;
    QPushButton *refresh_ = nullptr;
    std::function<bool(const NativeMonitorPreparation &)> preparationGuard_;
    void updateDevices(const NativeMonitorSnapshot &);
    bool chooseDevices();
    std::shared_ptr<const NativeMonitorPreparation> preparation_;
    QPushButton *prepare_,*start_,*cancel_,*inspect_,*keep_,*export_;
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
