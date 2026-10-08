// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "localization.hpp"
#include "history_resources_dialog.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SoundCurrent DAW"));
    QCoreApplication::setOrganizationName(QStringLiteral("SoundCurrent"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("language"),
        QApplication::translate("Main", "Interface language tag (draft catalogs available)"),
        QStringLiteral("tag")});
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"),
                                 QApplication::translate("Main", "Project folder to open"));
    parser.process(app);
    const auto preferences = soundcurrent::daw::i18n::loadPreferences();
    soundcurrent::daw::i18n::Runtime localization(
        soundcurrent::daw::i18n::chooseLanguage(parser.value("language"), preferences.language,
            qEnvironmentVariable("SOUNDCURRENT_DAW_LANGUAGE"), QLocale::system().uiLanguages()),
        preferences.formatLocale);
    soundcurrent::daw::ui::ControllerOptions options;
    options.historyBudget = soundcurrent::daw::ui::loadHistoryPreferences();
    const auto memory = soundcurrent::daw::ui::loadMemoryPreferences();
    options.memoryBytes = memory.totalBytes;
    options.snapshotBytes = memory.snapshotBytes;
    soundcurrent::daw::ui::StudioWindow window(nullptr, {}, {}, {}, {}, options,
                                               soundcurrent::daw::ui::saveHistoryPreferences,
                                               soundcurrent::daw::ui::saveMemoryPreferences);
    const auto paths = parser.positionalArguments();
    if (!paths.isEmpty()) {
#ifdef _WIN32
        window.openProject(std::filesystem::path(paths.front().toStdWString()));
#else
        window.openProject(soundcurrent::daw::utf8Path(paths.front().toUtf8().toStdString()));
#endif
    }
    window.show();
    return app.exec();
}
