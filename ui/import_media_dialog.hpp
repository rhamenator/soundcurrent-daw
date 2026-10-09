// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "wave_check_controller.hpp"
#include "media_copy_controller.hpp"
#include <QAbstractTableModel>
#include <QDialog>
#include <QCoreApplication>
class QLabel;
class QPushButton;
class QSpinBox;
class QTableView;
class QTimer;
namespace soundcurrent::daw::ui {
class ImportMediaModel : public QAbstractTableModel {
    Q_DECLARE_TR_FUNCTIONS(ImportMediaModel)
  public:
    ImportMediaModel(std::shared_ptr<const ImportInspectionReport>,ResourceLedger,QObject *parent=nullptr);
    int rowCount(const QModelIndex &index={}) const override {return index.isValid() ? 0 : static_cast<int>(rows_.size());}
    int columnCount(const QModelIndex &index={}) const override {return index.isValid() ? 0 : 6;}
    QVariant data(const QModelIndex &,int=Qt::DisplayRole) const override;
    QVariant headerData(int,Qt::Orientation,int) const override;
    bool canMap(int row) const;
    std::string_view originalReference(int row) const;
    void setResult(int,std::shared_ptr<const WaveCheckSnapshot>);
    void setCopyResult(int,std::shared_ptr<const MediaCopySnapshot>);
    std::optional<std::size_t> propertyOrdinal(int row) const;
    std::shared_ptr<const WaveCheckSnapshot> checked(int row) const;
    std::shared_ptr<const ImportInspectionReport> inspection() const {return source_;}
    void clearChecks();
    void retranslate();
  private:
    ResourceLease rowsGrant_;
    std::shared_ptr<const ImportInspectionReport> source_;
    struct Row {std::size_t property;std::shared_ptr<const WaveCheckSnapshot> result;std::shared_ptr<const MediaCopySnapshot> copied;};
    std::vector<Row> rows_;
    const ImportProperty *property(int) const;
};
class ImportMediaDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(ImportMediaDialog)
  public:
    ImportMediaDialog(QWidget *,std::shared_ptr<const ImportInspectionReport>,WaveCheckOptions,MediaCopyOptions={});
    bool checkReference(int row,const std::filesystem::path &approvedRoot);
    bool checkReplacement(int row,const std::filesystem::path &selectedFile);
    bool copyChecked(int row,const std::filesystem::path &destination);
    bool recoverCopied(const std::filesystem::path &operationFolder);
    bool recoverLastCopy();
    void requestShutdown();
    bool retired() const {return controller_.snapshot()->closed && copier_.snapshot()->closed;}
    std::shared_ptr<const WaveCheckSnapshot> snapshot() const {return controller_.snapshot();}
    std::shared_ptr<const MediaCopySnapshot> copySnapshot() const {return copier_.snapshot();}
  protected:
    void closeEvent(QCloseEvent *) override;
    void changeEvent(QEvent *) override;
  private:
    WaveCheckController controller_;
    MediaCopyController copier_;
    ImportMediaModel model_;
    QTableView *table_;
    QLabel *notice_,*folder_,*status_,*limitLabel_,*copyStatus_;
    QPushButton *chooseRoot_,*check_,*replace_,*clear_,*cancel_,*close_,*copy_,*recover_;
    QSpinBox *limit_;
    QTimer *timer_;
    std::filesystem::path root_;
    int activeRow_=-1;
    int activeCopyRow_=-1;
    bool closing_=false,admissionFailed_=false;
    void poll();
    void retranslate();
    int selectedRow() const;
};
} // namespace soundcurrent::daw::ui
