// SPDX-License-Identifier: GPL-3.0-only
// Adapted from the equalizer localization runtime; see reuse/reviews/2026-10-07-localization/.
#pragma once
#include <QApplication>
#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>
#include <memory>
class QDialog;
namespace soundcurrent::daw::i18n {
struct Language {
    QString tag, nativeName;
    int translated = 0, total = 0;
};
struct Preferences {
    QString language = QStringLiteral("system"), formatLocale = QStringLiteral("system");
};
QVector<Language> languages();
QString resolve(QString requested);
QString chooseLanguage(QString commandLine, QString stored, QString environment,
                       const QStringList &systemLanguages);
Preferences loadPreferences();
void savePreferences(const Preferences &);
// GUI startup/control only. No engine dependency and no live view reconstruction.
class Runtime {
  public:
    Runtime(QString requested, QString formatLocale);
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    QString requested() const { return requested_; }
    QString loaded() const { return loaded_; }
    bool catalogLoaded() const { return catalogLoaded_; }
  private:
    QString requested_, loaded_;
    QLocale previousLocale_;
    Qt::LayoutDirection previousDirection_;
    std::unique_ptr<QTranslator> translator_;
    bool catalogLoaded_ = false;
};
QDialog *settingsDialog(QWidget *parent = nullptr);
} // namespace soundcurrent::daw::i18n
