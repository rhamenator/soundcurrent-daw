// SPDX-License-Identifier: GPL-3.0-only
#include "equipment_profiles.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include <limits>
using namespace soundcurrent::daw::equipment;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F function) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid equipment data admitted");
}
QByteArray bytes(const Profile &profile) {
    return QJsonDocument(serialize(profile)).toJson();
}
QByteArray stored() {
    QFile file(libraryPath());
    check(file.open(QIODevice::ReadOnly), "Cannot read stored fixture");
    return file.readAll();
}
QDoubleSpinBox *gain(QDialog *editor) {
    auto *table = editor->findChild<QTableWidget *>("equipmentFilters");
    check(table != nullptr, "Filter table missing");
    auto *spin = qobject_cast<QDoubleSpinBox *>(table->cellWidget(0, 2));
    check(spin != nullptr, "Gain control missing");
    return spin;
}
QPushButton *button(QDialog *editor, const char *name) {
    auto *result = editor->findChild<QPushButton *>(name);
    check(result != nullptr, "Named editor control missing");
    return result;
}
void prompt(QMessageBox::StandardButton choice) {
    QTimer::singleShot(0, [choice] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        check(box && box->windowTitle() == "Save modified profile?", "Dirty prompt missing");
        box->button(choice)->click();
    });
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SoundCurrentDAWTest");
    QCoreApplication::setApplicationName("Equipment");
    try {
        Profile original;
        original.id = "test-source";
        original.kind = "microphone";
        original.brand = "Été Ελληνικά";
        original.family = "Measured";
        original.model = "Unit 123 / on axis";
        original.source = "https://example.org/measurement";
        original.conditions = "Synthetic test; relative magnitude, independently known source";
        original.filters.append({1000, -2, 1});
        original.response = {{20, 0}, {1000, 2}, {20000, 0}};
        check(parse(bytes(original)) == original, "Unicode round trip failed");
        auto whole = original;
        whole.kind = "whole_system";
        check(parse(bytes(whole)).kind == "whole_system" &&
                  serialize(whole).value("schema").toInt() == 3,
              "Whole-system kind or version missing");
        auto invalidSystem = serialize(whole);
        invalidSystem["schema"] = 2;
        rejects([&] { parse(QJsonDocument(invalidSystem).toJson()); });
        auto invalid = [&](const QString &key, const QJsonValue &value) {
            auto object = serialize(original);
            object[key] = value;
            rejects([&] { parse(QJsonDocument(object).toJson()); });
        };
        invalid("schema", 2.1);
        invalid("schema", "2");
        invalid("schema", 4);
        invalid("kind", "unsupported");
        invalid("custom", "true");
        invalid("model", 123);
        invalid("brand", "");
        invalid("conditions", "   ");
        invalid("phaseAlgorithm", "unsupported future field");
        invalid("measurementSource", "file:///etc/passwd");
        invalid("filters", QJsonArray{});
        invalid("response", "ignored?");
        invalid("id", QString(121, 'x'));
        invalid("model", QString(QChar(0)));
        invalid("response", QJsonArray{QJsonArray{1000, 0}, QJsonArray{100, 1}});
        invalid("filters", QJsonArray{QJsonObject{
                               {"type", "PK"}, {"frequency", 1000}, {"gain", 20}, {"q", 1}}});
        auto filters = serialize(original).value("filters").toArray();
        auto filter = filters[0].toObject();
        filter["extra"] = 1;
        invalid("filters", QJsonArray{filter});
        QJsonArray points;
        for (int i = 0; i < 4097; ++i)
            points.append(QJsonArray{20. + i, 0});
        invalid("response", points);
        auto duplicateJson = bytes(original);
        duplicateJson.insert(duplicateJson.indexOf('{') + 1, "\"kind\":\"speaker\",");
        rejects([&] { parse(duplicateJson); });
        auto duplicateFilter = bytes(original);
        const auto typeAt = duplicateFilter.indexOf("\"type\"");
        duplicateFilter.insert(typeAt, "\"gain\":0,");
        rejects([&] { parse(duplicateFilter); });
        rejects([] { parse(QByteArray(1024 * 1024 + 1, 'x')); });
        rejects([] { parse(QByteArray("{\"brand\":\"\xc0\x80\"}")); });
        rejects([] { parse(QByteArray(33, '[') + "0" + QByteArray(33, ']')); });
        check(
            parseResponseText("# comment\nFrequency Hz, dB\n20, -2, 0\n1000 1\n20000,0\n").size() ==
                3,
            "Response text format not supported");
        for (auto value : {"20 0\n10 0", "20 0", "20 nan\n1000 0", "20 0 0 9\n1000 0", "broken"})
            rejects([&] { parseResponseText(value); });
        rejects([] { parseResponseText(QByteArray(1024 * 1024 + 1, 'x')); });
        rejects([] { fitResponse({{1000, 0}, {100, 0}}); });
        rejects([] { fitResponse({{100, std::numeric_limits<double>::quiet_NaN()}, {1000, 0}}); });
        QVector<Point> measured;
        for (int i = 0; i < 128; ++i) {
            const double f = 20 * std::pow(1000., i / 127.);
            measured.append({f, filterResponseDb({2000, 3, 1}, 48000, f)});
        }
        const auto fitted = fitResponse(measured);
        double before = 0, after = 0;
        for (auto point : measured) {
            double residual = point.db;
            before += residual * residual;
            for (auto band : fitted)
                residual += filterResponseDb(band, 48000, point.frequency);
            after += residual * residual;
        }
        check(after < before * .2, "Correction fit sign or magnitude incorrect");
        for (const auto &band : fitResponse({{20, -5}, {100, -5}, {1000, 0}, {20000, 0}}))
            check(band.frequency >= 80 || band.gainDb <= 0, "Automatic sub-bass boost admitted");
        const auto catalog = bundledProfiles();
        check(catalog.size() == 1087, "Pinned catalog count changed");
        for (const auto &profile : catalog)
            check(parse(bytes(profile)) == profile, "Catalog round trip failed");
        saveLibrary({original});
        const auto initial = stored();
        rejects([&] { saveLibrary({original, original}); });
        check(stored() == initial, "Rejected duplicate changed library");
        auto bad = original;
        bad.filters[0].gainDb = 100;
        rejects([&] { saveLibrary({bad}); });
        check(stored() == initial, "Rejected invalid data changed library");
        check(loadLibrary() == QVector<Profile>{original}, "Library reopen changed reference");
        // Real modal editor: undo/redo, wheel guard, Cancel retains draft, window-close Discard.
        QTimer::singleShot(0, [&] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(editor && editor->objectName() == "equipmentEditor", "Editor missing");
            check(editor->size().height() <= editor->screen()->availableGeometry().height(),
                  "Editor exceeds screen");
            gain(editor)->setValue(-1.5);
            button(editor, "undoProfileEdit")->click();
            check(gain(editor)->value() == -2, "Editor undo failed");
            button(editor, "redoProfileEdit")->click();
            check(gain(editor)->value() == -1.5, "Editor redo failed");
            gain(editor)->clearFocus();
            QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), {}, QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(gain(editor), &wheel);
            check(gain(editor)->value() == -1.5, "Unfocused wheel changed correction");
            auto *plot = editor->findChild<QWidget *>("equipmentCurve");
            check(plot != nullptr, "Editable curve missing");
            const QRectF area(45, 15, plot->width() - 90, plot->height() - 40);
            const QPoint start =
                QPointF(area.left() + std::log(1000. / 20.) / std::log(1000.) * area.width(),
                        area.center().y() + 1.5 / 12 * area.height())
                    .toPoint();
            QTest::mousePress(plot, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(plot, start + QPoint(30, -20));
            QTest::mouseRelease(plot, Qt::LeftButton, Qt::NoModifier, start + QPoint(30, -20));
            check(gain(editor)->value() != -1.5, "Curve drag did not edit correction");
            button(editor, "undoProfileEdit")->click();
            auto *filterTable = editor->findChild<QTableWidget *>("equipmentFilters");
            check(gain(editor)->value() == -1.5 &&
                      qobject_cast<QDoubleSpinBox *>(filterTable->cellWidget(0, 1))->value() ==
                          1000,
                  "One curve undo did not restore frequency and gain");
            prompt(QMessageBox::Cancel);
            editor->reject();
            check(editor->isVisible() && gain(editor)->value() == -1.5, "Cancel lost editor draft");
            if (app.arguments().contains("--screenshots"))
                editor->grab().save(QDir::currentPath() + "/.cache/equipment-editor-draft.png");
            prompt(QMessageBox::Discard);
            editor->close();
        });
        saveNewProfile(nullptr, original);
        check(stored() == initial, "Discard changed library");
        // Escape triggers Save prompt; saved reference remains intact and new identity is
        // generated.
        QTimer::singleShot(0, [&] {
            auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(editor != nullptr, "Editor missing on save");
            gain(editor)->setValue(1.5);
            prompt(QMessageBox::Save);
            QTest::keyClick(editor, Qt::Key_Escape);
        });
        saveNewProfile(nullptr, original);
        const auto saved = loadLibrary();
        check(saved.size() == 2 && saved[0] == original && saved[1].custom &&
                  saved[1].id != original.id && saved[1].filters[0].gainDb == 1.5 &&
                  saved[1].provenance.contains(original.id),
              "Saving custom copy changed reference or lost provenance");
        // Exercise actual searchable library / editor entry. Editing cannot implicitly apply DSP.
        QTimer::singleShot(0, [&] {
            auto *library = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            check(library && library->objectName() == "equipmentLibrary", "Library missing");
            auto *search = library->findChild<QLineEdit *>("equipmentSearch");
            check(search != nullptr, "Library search missing");
            search->setText("Unit 123");
            if (app.arguments().contains("--screenshots")) {
                library->grab().save(QDir::currentPath() + "/.cache/equipment-library.png");
            }
            QTimer::singleShot(0, [&] {
                auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(editor && editor->objectName() == "equipmentEditor",
                      "Library edit did not open editor");
                if (app.arguments().contains("--screenshots"))
                    editor->grab().save(QDir::currentPath() + "/.cache/equipment-editor.png");
                editor->reject();
            });
            button(library, "editEquipmentProfile")->click();
            library->reject();
        });
        openLibrary(nullptr);
        check(loadLibrary() == saved, "Unchanged library visit saved data");
        std::cout << checks << " equipment checks passed; 1087 catalog entries validated.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
