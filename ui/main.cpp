// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SoundCurrent DAW"));
    QCoreApplication::setOrganizationName(QStringLiteral("SoundCurrent"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"),
                                 QApplication::translate("Main", "Project folder to open"));
    parser.process(app);
    soundcurrent::daw::ui::StudioWindow window;
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
