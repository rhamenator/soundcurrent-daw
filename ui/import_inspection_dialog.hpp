// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import_inspection_controller.hpp"
#include <QDialog>
#include <QAbstractTableModel>
#include <QCoreApplication>
class QLabel;
class QPushButton;
class QTimer;
class QTabWidget;
namespace soundcurrent::daw::ui {
class ImportPreviewModel : public QAbstractTableModel {
    Q_DECLARE_TR_FUNCTIONS(ImportPreviewModel)
  public:
    explicit ImportPreviewModel(QObject *parent=nullptr) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &index={}) const override { return index.isValid() || !report_ ? 0 : int(report_->nodes().size()); }
    int columnCount(const QModelIndex &index={}) const override { return index.isValid() ? 0 : 5; }
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole) const override;
    void setReport(std::shared_ptr<const ImportInspectionReport>);
  private:
    std::shared_ptr<const ImportInspectionReport> report_;
};
class ImportPropertyModel : public QAbstractTableModel {
    Q_DECLARE_TR_FUNCTIONS(ImportPropertyModel)
  public:
    explicit ImportPropertyModel(QObject *parent=nullptr) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &index={}) const override { return index.isValid() || !report_ ? 0 : int(report_->properties().size()); }
    int columnCount(const QModelIndex &index={}) const override { return index.isValid() ? 0 : 6; }
    QVariant data(const QModelIndex &,int role=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int role=Qt::DisplayRole) const override;
    void setReport(std::shared_ptr<const ImportInspectionReport> report) { beginResetModel(); report_=std::move(report); endResetModel(); }
  private:
    std::shared_ptr<const ImportInspectionReport> report_;
    static QString fieldName(ImportPropertyId);
    static QString unit(ImportPropertyId);
    static QString status(ImportEvidenceStatus);
    static QString reason(ImportEvidenceReason);
};
class ImportInspectionDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(ImportInspectionDialog)
  public:
    explicit ImportInspectionDialog(QWidget *,InspectionOptions);
    bool inspect(const std::filesystem::path &);
    bool openInspection(const std::filesystem::path &);
    bool saveInspection(const std::filesystem::path &);
    void requestShutdown();
    bool retired() const { return controller_.snapshot()->closed; }
    std::shared_ptr<const InspectionSnapshot> snapshot() const { return controller_.snapshot(); }
  protected:
    void closeEvent(QCloseEvent *) override;
    void changeEvent(QEvent *) override;
  private:
    ImportInspectionController controller_;
    ImportPreviewModel model_;
    ImportPropertyModel properties_;
    QTabWidget *tabs_;
    QLabel *file_,*summary_,*notice_;
    QPushButton *choose_,*open_,*save_,*cancel_,*close_;
    QTimer *timer_;
    bool closing_=false;
    std::shared_ptr<const ImportInspectionReport> shown_;
    int valueCount_ = 0;
    void poll();
    void retranslate();
};
} // namespace soundcurrent::daw::ui
