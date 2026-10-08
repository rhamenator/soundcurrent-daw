// SPDX-License-Identifier: GPL-3.0-only
// Modified equalizer runtime, fallback, pseudo translation and settings selector.
// Exact origins and modifications: reuse/reviews/2026-10-07-localization/.
#include "localization.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QMap>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
static void initializeLocalizationResources() {
    static const bool initialized = [] { Q_INIT_RESOURCE(daw_translations); return true; }();
    (void)initialized;
}
namespace soundcurrent::daw::i18n {
namespace {
class Text {
    Q_DECLARE_TR_FUNCTIONS(Localization)
};
QString normalize(QString tag) {
    return tag.trimmed().replace('_', '-');
}
bool validTag(const QString &tag) {
    static const QRegularExpression syntax(QStringLiteral("^[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*$"));
    return tag.size() <= 64 && syntax.match(tag).hasMatch();
}
class PseudoTranslator final : public QTranslator {
  public:
    explicit PseudoTranslator(bool rtl) : rtl_(rtl) {}
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override {
        if (!source) return {};
        const QString input = QString::fromUtf8(source);
        QString out;
        static const QRegularExpression protectedParts(QStringLiteral("(<[^>]*>|%L?(?:[0-9]+|n)|&&|&)"));
        auto append = [&](const QString &s) {
            const QString plain = QStringLiteral("aeiouAEIOU"), accented = QString::fromUtf8("áëïöüÁËÏÖÜ");
            for (const auto c : s) {
                const auto i = plain.indexOf(c);
                out += i < 0 ? c : accented[i];
                if (c.isLetter()) out += c;
            }
        };
        auto matches = protectedParts.globalMatch(input);
        qsizetype pos = 0;
        while (matches.hasNext()) {
            const auto m = matches.next();
            append(input.mid(pos, m.capturedStart() - pos));
            out += m.captured(); pos = m.capturedEnd();
        }
        append(input.mid(pos));
        return (rtl_ ? QString::fromUtf8("\u2067[אב ") : QStringLiteral("[")) + out +
               (rtl_ ? QString::fromUtf8("]\u2069") : QStringLiteral("]"));
    }
  private:
    bool rtl_;
};
}
QVector<Language> languages() {
    initializeLocalizationResources();
    QFile file(QStringLiteral(":/daw/i18n/catalogs.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 512 * 1024) return {};
    QVector<Language> result;
    for (const auto &v : QJsonDocument::fromJson(file.readAll()).array()) {
        const auto o = v.toObject();
        const Language l{o["tag"].toString(), o["name"].toString(),
                         o["translated"].toInt(), o["total"].toInt()};
        if (validTag(l.tag) && !l.nativeName.isEmpty() && l.translated > 0 &&
            l.translated <= l.total) result.append(l);
    }
    return result;
}
QString resolve(QString requested) {
    const auto tag = normalize(std::move(requested));
    if (tag == "qps-ploc" || tag == "qps-rtl") return tag;
    if (!validTag(tag)) return QStringLiteral("en");
    for (const auto &l : languages())
        if (l.tag.compare(tag, Qt::CaseInsensitive) == 0) return l.tag;
    // Explicit application fallback policy. Never strip or infer a script.
    const QMap<QString, QString> fallbacks{{"de-DE", "de"}, {"de-AT", "de"}, {"de-CH", "de"},
        {"fr-FR", "fr"}, {"fr-BE", "fr"}, {"fr-CH", "fr"}, {"fr-CA", "fr"},
        {"zh-Hant-TW", "zh-Hant"}, {"zh-Hans-CN", "zh-Hans"}};
    for (auto it = fallbacks.begin(); it != fallbacks.end(); ++it)
        if (tag.compare(it.key(), Qt::CaseInsensitive) == 0)
            for (const auto &l : languages()) if (l.tag == it.value()) return l.tag;
    return QStringLiteral("en");
}
QString chooseLanguage(QString commandLine, QString stored, QString environment,
                       const QStringList &systemLanguages) {
    if (!commandLine.isEmpty()) return commandLine;
    if (!stored.isEmpty() && stored != "system") return stored;
    if (!environment.isEmpty()) return environment;
    for (const auto &tag : systemLanguages)
        if (resolve(tag) != "en" || normalize(tag).startsWith("en", Qt::CaseInsensitive))
            return tag;
    return QStringLiteral("en");
}
Preferences loadPreferences() {
    const QSettings settings;
    return {settings.value("i18n/language", "system").toString(),
            settings.value("i18n/formatLocale", "system").toString()};
}
void savePreferences(const Preferences &p) {
    QSettings settings;
    settings.setValue("i18n/language", p.language);
    settings.setValue("i18n/formatLocale", p.formatLocale);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error(Text::tr("Language preferences could not be saved.").toStdString());
}
Runtime::Runtime(QString requested, QString format)
    : requested_(std::move(requested)), loaded_(resolve(requested_)),
      previousLocale_(QLocale()), previousDirection_(QApplication::layoutDirection()) {
    const auto normalizedFormat = normalize(format);
    auto locale = normalizedFormat == "system" ? QLocale::system() : QLocale(normalizedFormat);
    if (!validTag(normalizedFormat) || locale.language() == QLocale::C) locale = QLocale::system();
    QLocale::setDefault(locale);
    if (loaded_.startsWith("qps-")) {
        translator_ = std::make_unique<PseudoTranslator>(loaded_ == "qps-rtl");
    } else {
        translator_ = std::make_unique<QTranslator>();
        if (!translator_->load(":/daw/i18n/soundcurrent_daw_" + loaded_ + ".qm")) {
            loaded_ = "en";
            (void)translator_->load(QStringLiteral(":/daw/i18n/soundcurrent_daw_en.qm"));
        }
    }
    catalogLoaded_ = QCoreApplication::installTranslator(translator_.get());
    QApplication::setLayoutDirection(loaded_ == "qps-rtl" ? Qt::RightToLeft
        : loaded_.startsWith("qps-") ? Qt::LeftToRight : QLocale(loaded_).textDirection());
}
Runtime::~Runtime() {
    if (catalogLoaded_) QCoreApplication::removeTranslator(translator_.get());
    QLocale::setDefault(previousLocale_);
    QApplication::setLayoutDirection(previousDirection_);
}
QDialog *settingsDialog(QWidget *parent) {
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("languageSettingsDialog"));
    dialog->setWindowTitle(Text::tr("Language and regional settings"));
    auto *form = new QFormLayout(dialog);
    const auto preferences = loadPreferences();
    auto *language = new QComboBox(dialog);
    language->setObjectName(QStringLiteral("uiLanguage"));
    language->setAccessibleName(Text::tr("Interface language"));
    language->addItem(Text::tr("Use system language"), "system");
    for (const auto &l : languages())
        language->addItem(l.nativeName + " (" + l.tag + ")", l.tag);
    language->addItem(Text::tr("Expanded test language"), "qps-ploc");
    language->addItem(Text::tr("Right-to-left test language"), "qps-rtl");
    auto index = language->findData(preferences.language);
    if (index < 0) {
        language->addItem(Text::tr("Saved language preference: %1").arg(preferences.language), preferences.language);
        index = language->count() - 1;
    }
    language->setCurrentIndex(index);
    form->addRow(Text::tr("Interface language"), language);
    auto *format = new QComboBox(dialog);
    format->setObjectName(QStringLiteral("formatLocale"));
    format->setAccessibleName(Text::tr("Number and date format"));
    format->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    format->setMinimumContentsLength(24); format->setMaxVisibleItems(12);
    auto *view = new QListView(format); view->setUniformItemSizes(true); format->setView(view);
    format->addItem(Text::tr("Use system locale"), "system");
    auto locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory);
    std::sort(locales.begin(), locales.end(), [](const QLocale &a, const QLocale &b) { return a.name() < b.name(); });
    QStringList seen;
    for (const auto &locale : locales) {
        if (locale.language() == QLocale::C) continue;
        auto tag = locale.bcp47Name();
        const auto territory = QLocale::territoryToCode(locale.territory());
        if (!territory.isEmpty() && !tag.endsWith('-' + territory)) tag += '-' + territory;
        if (seen.contains(tag)) continue;
        seen.append(tag);
        format->addItem(locale.nativeLanguageName() + " — " + locale.nativeTerritoryName() + " (" + tag + ")", tag);
    }
    index = format->findData(preferences.formatLocale);
    if (index < 0) {
        format->addItem(Text::tr("Saved format preference: %1").arg(preferences.formatLocale), preferences.formatLocale);
        index = format->count() - 1;
    }
    format->setCurrentIndex(index);
    form->addRow(Text::tr("Number and date format"), format);
    auto *help = new QLabel(dialog); help->setWordWrap(true); help->setTextFormat(Qt::PlainText); form->addRow(help);
    auto update = [language, help] {
        auto requested = language->currentData().toString();
        if (requested == "system") requested = chooseLanguage({}, "system", qEnvironmentVariable("SOUNDCURRENT_DAW_LANGUAGE"), QLocale::system().uiLanguages());
        const auto tag = resolve(requested);
        int done = 0, total = 0;
        for (const auto &l : languages()) if (l.tag == tag) { done = l.translated; total = l.total; }
        help->setText(tag.startsWith("qps-") ? Text::tr("Developer test language; no translated language coverage.")
            : Text::tr("Requested: %1. Loaded after restart: %2. Translation coverage: %3 of %4 messages. Missing translations use English. Draft translations await native-speaker review.")
              .arg(requested, tag).arg(done).arg(total));
    };
    QObject::connect(language, &QComboBox::currentIndexChanged, dialog, [update] { update(); }); update();
    auto *restart = new QLabel(Text::tr("Restart the application to apply language and number format changes. Save projects before quitting."), dialog);
    restart->setWordWrap(true); form->addRow(restart);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        try { savePreferences({language->currentData().toString(), format->currentData().toString()}); dialog->accept(); }
        catch (const std::exception &e) { help->setText(QString::fromUtf8(e.what())); }
    });
    dialog->resize(600, 380);
    return dialog;
}
} // namespace soundcurrent::daw::i18n
