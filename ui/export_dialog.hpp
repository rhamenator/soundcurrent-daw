// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/export.hpp>
#include <QDialog>
#include <QCoreApplication>
#include <memory>
class QComboBox;
class QLineEdit;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
namespace soundcurrent::daw::ui {
struct ExportSelection {
    explicit ExportSelection(ExportSpec value) : spec(std::move(value)) {}
    ExportSpec spec;
    std::filesystem::path destination;
};
class ExportDialog : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(ExportDialog)
  public:
    ExportDialog(std::filesystem::path root, std::shared_ptr<const Session>, QWidget *parent);
    const std::optional<ExportSelection> &selection() const {
        return selection_;
    }
    void accept() override;

  private:
    std::shared_ptr<const Session> session_;
    QComboBox *track_;
    QLineEdit *start_, *end_, *destination_;
    QCheckBox *tail_, *rf64_;
    QDoubleSpinBox *tailLimit_;
    QLabel *validation_, *duration_;
    QPushButton *export_;
    std::optional<ExportSelection> selection_;
    void setRange();
    void validateFields();
    std::optional<ExportSelection> fields() const;
};
} // namespace soundcurrent::daw::ui
