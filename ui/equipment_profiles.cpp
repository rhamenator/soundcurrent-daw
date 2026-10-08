// SPDX-License-Identifier: GPL-3.0-only
// Modified 2026-10-05 from the pinned equalizer snapshot; see reuse/equipment/provenance.json.
#include "equipment_profiles.hpp"
#include "accelerating_spinbox.hpp"
#include <QApplication>
#include <QAction>
#include <QScreen>
#include <QScrollArea>
#include <QWheelEvent>
#include <QSet>
#include <QJsonParseError>
#include <soundcurrent/session.hpp>
#include <vector>
#include <set>
#include <nlohmann/json.hpp>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>
// Referencing the generated resource symbol keeps it linked from the static UI library.
static void initializeEquipmentResources() {
    Q_INIT_RESOURCE(equipment_resources);
}
namespace soundcurrent::daw::equipment {
namespace {
class EquipmentText {
    Q_DECLARE_TR_FUNCTIONS(EquipmentProfiles)
};
class FocusSpin : public soundcurrent::daw::widgets::AcceleratingDoubleSpinBox {
  public:
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus())
            soundcurrent::daw::widgets::AcceleratingDoubleSpinBox::wheelEvent(event);
        else
            event->ignore();
    }
};
class FocusCombo : public QComboBox {
  public:
    void wheelEvent(QWheelEvent *event) override {
        if (hasFocus())
            QComboBox::wheelEvent(event);
        else
            event->ignore();
    }
};
QVBoxLayout *scrollLayout(QDialog *dialog) {
    auto *root = new QVBoxLayout(dialog);
    auto *scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *content = new QVBoxLayout(body);
    scroll->setWidget(body);
    root->addWidget(scroll);
    return content;
}
void fitWindow(QDialog *dialog) {
    const auto available = dialog->screen()->availableGeometry();
    dialog->resize(std::min(dialog->width(), available.width() - 32),
                   std::min(dialog->height(), available.height() - 32));
}
void require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(EquipmentText::tr(reason).toUtf8().toStdString());
}
int ask(QWidget *parent, const QString &title, const QString &text,
        QMessageBox::StandardButtons buttons,
        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton) {
    QMessageBox box(QMessageBox::Question, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    box.setDefaultButton(defaultButton);
    return box.exec();
}
QString typeName(FilterType t) {
    return t == FilterType::LowShelf ? "LS" : t == FilterType::HighShelf ? "HS" : "PK";
}
double responseAt(const QVector<Point> &points, double f) {
    if (f <= points.front().frequency)
        return points.front().db;
    for (int i = 1; i < points.size(); ++i)
        if (f <= points[i].frequency) {
            const auto a = points[i - 1], b = points[i];
            return a.db +
                   (b.db - a.db) * std::log(f / a.frequency) / std::log(b.frequency / a.frequency);
        }
    return points.back().db;
}
class Plot : public QWidget {
  public:
    Profile profile;
    std::function<void(int, double, double)> onDrag;
    std::function<void()> onDragBegin, onDragEnd;
    int dragging = -1;
    double rangeDb() const {
        double peak = 6;
        for (const auto &pt : profile.response)
            peak = std::max(peak, std::abs(pt.db));
        for (int i = 0; i < 40; ++i) {
            double db = 0;
            const double f = 20 * std::pow(1000., i / 39.);
            for (const auto &b : profile.filters)
                db += filterResponseDb(b, 48000, f);
            peak = std::max(peak, std::abs(db));
        }
        return std::min(24., std::ceil(peak / 6.) * 6.);
    }
    Plot() {
        setObjectName("equipmentCurve");
        setLayoutDirection(Qt::LeftToRight);
        setMinimumHeight(180);
        setAccessibleName(EquipmentText::tr("Published response and editable correction curves"));
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (!onDrag || e->button() != Qt::LeftButton)
            return;
        const QRectF area(45, 15, width() - 90, height() - 40);
        dragging = -1;
        double best = 20;
        for (int i = 0; i < profile.filters.size(); ++i) {
            const auto &b = profile.filters[i];
            const QPointF point(area.left() +
                                    std::log(b.frequency / 20.) / std::log(1000.) * area.width(),
                                area.center().y() - b.gainDb / (2 * rangeDb()) * area.height());
            const auto delta = point - e->position();
            const double distance = std::hypot(delta.x(), delta.y());
            if (distance < best) {
                best = distance;
                dragging = i;
            }
        }
        if (dragging >= 0 && onDragBegin)
            onDragBegin();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!onDrag || dragging < 0)
            return;
        const QRectF area(45, 15, width() - 90, height() - 40);
        const double f =
            20. *
            std::pow(1000., std::clamp((e->position().x() - area.left()) / area.width(), 0., 1.));
        const double gain = std::clamp(
            (area.center().y() - e->position().y()) / area.height() * (2 * rangeDb()), -6., 6.);
        onDrag(dragging, f, gain);
    }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (dragging >= 0 && onDragEnd)
            onDragEnd();
        dragging = -1;
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setLayoutDirection(Qt::LeftToRight);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#172337"));
        const QRectF area(45, 15, width() - 90, height() - 40);
        const double range = rangeDb();
        auto x = [&](double f) {
            return area.left() + std::log(f / 20.) / std::log(1000.) * area.width();
        };
        auto y = [&](double d) {
            return area.center().y() - std::clamp(d, -range, range) / (2 * range) * area.height();
        };
        p.setPen(QColor("#718096"));
        for (double f : {20., 100., 1000., 10000., 20000.}) {
            p.drawLine(QPointF(x(f), area.top()), QPointF(x(f), area.bottom()));
            p.drawText(QPointF(x(f) - 10, area.bottom() + 17), QString::number(f));
        }
        for (double d : {-rangeDb(), -rangeDb() / 2, 0., rangeDb() / 2, rangeDb()}) {
            p.drawLine(QPointF(area.left(), y(d)), QPointF(area.right(), y(d)));
            p.drawText(QPointF(2, y(d) + 4), QString::number(d));
        }
        auto draw = [&](QColor color, auto db, double low = 20, double high = 20000) {
            QPainterPath path;
            for (int i = 0; i <= 400; ++i) {
                const double f = low * std::pow(high / low, i / 400.);
                const QPointF v(x(f), y(db(f)));
                if (i)
                    path.lineTo(v);
                else
                    path.moveTo(v);
            }
            p.setPen(QPen(color, 2));
            p.drawPath(path);
        };
        if (!profile.response.isEmpty() && profile.response.back().frequency >= 20 &&
            profile.response.front().frequency <= 20000)
            draw(
                QColor("#f6ad55"), [&](double f) { return responseAt(profile.response, f); },
                std::max(20., profile.response.front().frequency),
                std::min(20000., profile.response.back().frequency));
        draw(QColor("#4fd1c5"), [&](double f) {
            double d = 0;
            for (const auto &b : profile.filters)
                d += filterResponseDb(b, 48000, f);
            return d;
        });
        if (onDrag) {
            p.setPen(QColor("#4fd1c5"));
            p.setBrush(QColor("#4fd1c5"));
            for (const auto &b : profile.filters)
                p.drawEllipse(QPointF(x(b.frequency), y(b.gainDb)), 4, 4);
        }
    }
};
class Editor : public QDialog {
  public:
    Profile draft;
    bool dirty = false, restoring = false, inGesture = false;
    Profile initial;
    std::vector<Profile> history;
    std::size_t cursor = 0;
    QAction *undoAction, *redoAction;
    QString originalId, originalProvenance;
    QTableWidget *table;
    Plot *plot;
    QLineEdit *brand, *family, *model, *source, *conditions, *equipmentType, *powerType;
    std::function<bool(Profile)> save;
    Editor(Profile profile, QWidget *parent, std::function<bool(Profile)> writer)
        : QDialog(parent), draft(std::move(profile)), save(std::move(writer)) {
        initial = draft;
        history.push_back(draft);
        originalId = draft.id;
        originalProvenance = draft.provenance;
        setObjectName("equipmentEditor");
        setWindowTitle(EquipmentText::tr("Equipment profile editor"));
        resize(760, 650);
        auto *layout = scrollLayout(this);
        auto *form = new QFormLayout;
        auto field = [&](const char *key, const QString &value) {
            auto *e = new QLineEdit(value);
            e->setMaxLength(QString::fromLatin1(key) == "Source"       ? 2048
                            : QString::fromLatin1(key) == "Conditions" ? 2000
                                                                       : 120);
            e->setCursorPosition(0);
            form->addRow(EquipmentText::tr(key), e);
            QObject::connect(e, &QLineEdit::textEdited, this, [this] { changed(); });
            return e;
        };
        brand = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Brand"), draft.brand);
        family = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Family"), draft.family);
        equipmentType = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Equipment subtype"), draft.equipmentType);
        equipmentType->setObjectName("equipmentSubtypeEdit");
        powerType = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Active / passive / unknown"), draft.powerType);
        powerType->setObjectName("equipmentPowerEdit");
        model = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Model"), draft.model);
        source = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Source"), draft.source);
        conditions = field(QT_TRANSLATE_NOOP("EquipmentProfiles", "Conditions"), draft.conditions);
        layout->addLayout(form);
        auto *legend = new QLabel(
            "Orange: measured response where supplied. Teal: correction at 48 kHz. Drag teal "
            "control points "
            "or edit the table. Saving preserves the reference and creates a custom copy.");
        legend->setWordWrap(true);
        layout->addWidget(legend);
        plot = new Plot;
        plot->profile = draft;
        layout->addWidget(plot);
        table = new QTableWidget(static_cast<int>(draft.filters.size()), 4);
        table->setObjectName("equipmentFilters");
        table->setMaximumHeight(150);
        table->setHorizontalHeaderLabels({EquipmentText::tr("Type"), EquipmentText::tr("Frequency Hz"), EquipmentText::tr("Gain dB"), EquipmentText::tr("Q")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        layout->addWidget(table);
        for (int i = 0; i < draft.filters.size(); ++i)
            addRow(i, draft.filters[i]);
        undoAction = new QAction(EquipmentText::tr("Undo profile edit"), this);
        redoAction = new QAction(EquipmentText::tr("Redo profile edit"), this);
        undoAction->setShortcut(QKeySequence::Undo);
        redoAction->setShortcut(QKeySequence::Redo);
        undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(undoAction);
        addAction(redoAction);
        QObject::connect(undoAction, &QAction::triggered, this, [this] { restore(-1); });
        QObject::connect(redoAction, &QAction::triggered, this, [this] { restore(1); });
        plot->onDragBegin = [this] { inGesture = true; };
        plot->onDragEnd = [this] {
            inGesture = false;
            remember();
        };
        plot->onDrag = [this](int r, double f, double g) {
            if (r >= table->rowCount())
                return;
            qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 1))->setValue(f);
            qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 2))->setValue(g);
        };
        auto *row = new QHBoxLayout;
        auto *add = new QPushButton(EquipmentText::tr("Add filter"));
        auto *remove = new QPushButton(EquipmentText::tr("Remove selected filter"));
        row->addWidget(add);
        row->addWidget(remove);
        layout->addLayout(row);
        QObject::connect(add, &QPushButton::clicked, this, [this] {
            if (table->rowCount() >= 16)
                return;
            const int r = table->rowCount();
            table->insertRow(r);
            addRow(r, {1000, 0, 1});
            changed();
        });
        QObject::connect(remove, &QPushButton::clicked, this, [this] {
            if (table->currentRow() >= 0 && table->rowCount() > 1) {
                table->removeRow(table->currentRow());
                changed();
            }
        });
        auto *undoButton = new QPushButton(EquipmentText::tr("Undo"));
        auto *redoButton = new QPushButton(EquipmentText::tr("Redo"));
        undoButton->setObjectName("undoProfileEdit");
        redoButton->setObjectName("redoProfileEdit");
        row->addWidget(undoButton);
        row->addWidget(redoButton);
        QObject::connect(undoButton, &QPushButton::clicked, undoAction, &QAction::trigger);
        QObject::connect(redoButton, &QPushButton::clicked, redoAction, &QAction::trigger);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        static_cast<QVBoxLayout *>(this->layout())->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            if (persist())
                accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &Editor::reject);
    }
    void addRow(int r, const EqBand &b) {
        auto *type = new FocusCombo;
        type->addItems({"PK", "LS", "HS"});
        type->setCurrentText(typeName(b.type));
        table->setCellWidget(r, 0, type);
        QObject::connect(type, &QComboBox::currentIndexChanged, this, [this] { changed(); });
        for (int c = 1; c < 4; ++c) {
            auto *spin = new FocusSpin;
            spin->setDecimals(c == 1 ? 1 : 2);
            spin->setRange(c == 1 ? 20 : c == 2 ? -6 : .1, c == 1 ? 20000 : c == 2 ? 6 : 6);
            spin->setValue(c == 1 ? b.frequency : c == 2 ? b.gainDb : b.q);
            spin->setAccessibleName(table->horizontalHeaderItem(c)->text());
            table->setCellWidget(r, c, spin);
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
        }
    }
    void remember() {
        if (draft == history[cursor])
            return;
        history.resize(cursor + 1);
        if (history.size() == 256) {
            history.erase(history.begin());
            --cursor;
        }
        history.push_back(draft);
        ++cursor;
    }
    void restore(int direction) {
        if ((direction < 0 && cursor == 0) || (direction > 0 && cursor + 1 == history.size()))
            return;
        if (auto *focused = focusWidget())
            focused->clearFocus();
        if (direction < 0)
            --cursor;
        else
            ++cursor;
        draft = history[cursor];
        restoring = true;
        brand->setText(draft.brand);
        family->setText(draft.family);
        equipmentType->setText(draft.equipmentType);
        powerType->setText(draft.powerType);
        model->setText(draft.model);
        source->setText(draft.source);
        conditions->setText(draft.conditions);
        table->setRowCount(0);
        table->setRowCount(static_cast<int>(draft.filters.size()));
        for (int r = 0; r < draft.filters.size(); ++r)
            addRow(r, draft.filters[r]);
        restoring = false;
        dirty = !(draft == initial);
        plot->profile = draft;
        plot->update();
    }
    void changed() {
        if (restoring)
            return;
        draft.brand = brand->text();
        draft.family = family->text();
        draft.equipmentType = equipmentType->text();
        draft.powerType = powerType->text();
        draft.model = model->text();
        draft.source = source->text();
        draft.conditions = conditions->text();
        draft.filters.clear();
        for (int r = 0; r < table->rowCount(); ++r) {
            const auto t = qobject_cast<QComboBox *>(table->cellWidget(r, 0))->currentText();
            draft.filters.append({qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 1))->value(),
                                  qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 2))->value(),
                                  qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 3))->value(),
                                  t == "LS"   ? FilterType::LowShelf
                                  : t == "HS" ? FilterType::HighShelf
                                              : FilterType::Peaking});
        }
        dirty = !(draft == initial);
        if (!inGesture)
            remember();
        plot->profile = draft;
        plot->update();
    }
    bool persist() {
        draft.brand = brand->text().trimmed();
        draft.family = family->text().trimmed();
        draft.equipmentType = equipmentType->text().trimmed();
        draft.powerType = powerType->text().trimmed();
        draft.model = model->text().trimmed();
        draft.source = source->text();
        draft.conditions = conditions->text();
        draft.custom = true;
        draft.provenance =
            originalProvenance.left(1800) +
            (originalId.isEmpty() ? "\nUser-created profile" : "\nCustom copy of " + originalId);
        draft.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (save(draft)) {
            dirty = false;
            return true;
        }
        return false;
    }
    void reject() override {
        if (dirty) {
            const auto answer = ask(
                this, EquipmentText::tr("Save modified profile?"),
                EquipmentText::tr("This profile has changed. Save a custom copy before leaving?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (answer == QMessageBox::Cancel)
                return;
            if (answer == QMessageBox::Save && !persist())
                return;
        }
        QDialog::reject();
    }
};
} // namespace
QJsonObject serialize(const Profile &p) {
    QJsonArray filters, response;
    for (const auto &b : p.filters)
        filters.append(QJsonObject{{"type", typeName(b.type)},
                                   {"frequency", b.frequency},
                                   {"gain", b.gainDb},
                                   {"q", b.q}});
    for (const auto &v : p.response)
        response.append(QJsonArray{v.frequency, v.db});
    return {{"schema", p.kind == "whole_system" ? 3 : 2},
            {"id", p.id},
            {"kind", p.kind},
            {"brand", p.brand},
            {"family", p.family},
            {"equipmentType", p.equipmentType},
            {"powerType", p.powerType},
            {"model", p.model},
            {"measurementSource", p.source},
            {"conditions", p.conditions},
            {"provenance", p.provenance},
            {"custom", p.custom},
            {"filters", filters},
            {"response", response}};
}
Profile parse(const QByteArray &bytes) {
    require(bytes.size() <= 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile exceeds the 1 MiB limit."));
    require(soundcurrent::daw::validUtf8(
                std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))), QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile is not valid UTF-8."));
    int depth = 0;
    bool quoted = false, escaped = false;
    for (char c : bytes) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[')
            require(++depth <= 32, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile exceeds nesting limit."));
        else if (c == '}' || c == ']')
            --depth;
    }
    // Qt otherwise keeps the last duplicate key. Reject ambiguous exchange files before
    // creating the GUI model; the existing bounded JSON library is already vendored.
    std::vector<std::set<std::string>> objectKeys;
    using Json = nlohmann::json;
    const auto uniqueKeys = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start)
            objectKeys.emplace_back();
        else if (event == Json::parse_event_t::key)
            require(!objectKeys.empty() &&
                        objectKeys.back().insert(value.get<std::string>()).second, QT_TRANSLATE_NOOP("EquipmentProfiles", "Duplicate JSON field in equipment profile."));
        else if (event == Json::parse_event_t::object_end)
            objectKeys.pop_back();
        return true;
    };
    try {
        const auto validated =
            Json::parse(bytes.constData(), bytes.constData() + bytes.size(), uniqueKeys);
        require(validated.is_object(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Expected a JSON equipment profile."));
    } catch (const Json::exception &) {
        require(false, QT_TRANSLATE_NOOP("EquipmentProfiles", "Invalid equipment JSON."));
    }
    const auto document = QJsonDocument::fromJson(bytes);
    require(document.isObject(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Expected a JSON equipment profile. Import response text using "
                                 "the response import button."));
    const auto o = document.object();
    const QSet<QString> keys{"schema",
                             "id",
                             "kind",
                             "brand",
                             "family",
                             "model",
                             "measurementSource",
                             "conditions",
                             "provenance",
                             "custom",
                             "filters",
                             "response",
                             "equipmentType",
                             "powerType"};
    for (auto it = o.begin(); it != o.end(); ++it)
        require(keys.contains(it.key()), QT_TRANSLATE_NOOP("EquipmentProfiles", "Unknown profile field; import would lose data."));
    for (const auto &key : {"id", "kind", "brand", "family", "model", "measurementSource",
                            "conditions", "provenance", "equipmentType", "powerType"})
        require(!o.contains(key) || o.value(key).isString(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile metadata must be text."));
    require(!o.contains("custom") || o.value("custom").isBool(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Custom flag must be boolean."));
    require(o.value("filters").isArray() &&
                (!o.contains("response") || o.value("response").isArray()), QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile filters and response must be arrays."));
    require(o.value("schema").isDouble() &&
                (o.value("schema").toDouble() == 2 || o.value("schema").toDouble() == 3), QT_TRANSLATE_NOOP("EquipmentProfiles", "Unsupported equipment profile schema (expected 2 or 3)."));
    Profile p;
    p.id = o.value("id").toString();
    p.kind = o.value("kind").toString();
    require(p.kind != "whole_system" || o.value("schema").toDouble() == 3, QT_TRANSLATE_NOOP("EquipmentProfiles", "Whole-system profiles require DAW schema 3."));
    p.brand = o.value("brand").toString().trimmed();
    p.family = o.value("family").toString().trimmed();
    p.equipmentType = o.value("equipmentType").toString("Unclassified").trimmed();
    p.powerType = o.value("powerType").toString("Unknown").trimmed();
    require(!p.equipmentType.isEmpty() && p.equipmentType.size() <= 120 && !p.powerType.isEmpty() &&
                p.powerType.size() <= 120, QT_TRANSLATE_NOOP("EquipmentProfiles", "Equipment subtype and power type are required (maximum 120 characters each)."));
    p.model = o.value("model").toString().trimmed();
    p.source = o.value("measurementSource").toString();
    p.conditions = o.value("conditions").toString();
    p.provenance = o.value("provenance").toString();
    p.custom = o.value("custom").toBool();
    require(QStringList{"speaker", "microphone", "amplifier", "whole_system"}.contains(p.kind), QT_TRANSLATE_NOOP("EquipmentProfiles", "Equipment kind must be speaker, microphone, amplifier or whole_system."));
    require(!p.brand.isEmpty() && !p.family.isEmpty() && !p.model.isEmpty() &&
                p.brand.size() <= 120 && p.family.size() <= 120 && p.model.size() <= 120, QT_TRANSLATE_NOOP("EquipmentProfiles", "Brand, family and model are required (maximum 120 characters each)."));
    require(p.id.size() <= 120 && p.conditions.size() <= 2000 && p.provenance.size() <= 2000 &&
                p.source.size() <= 2048, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile metadata is too long."));
    for (const auto &metadata : {p.id, p.kind, p.brand, p.family, p.model, p.source, p.conditions,
                                 p.provenance, p.equipmentType, p.powerType})
        require(!metadata.contains(QChar(0)), QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile metadata contains a NUL character."));
    require(!p.conditions.trimmed().isEmpty(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Measurement conditions are required."));
    const QUrl url(p.source);
    require(p.custom || (url.isValid() && url.scheme() == "https" && !url.host().isEmpty()), QT_TRANSLATE_NOOP("EquipmentProfiles", "Published profiles need an HTTPS measurement source."));
    const auto filters = o.value("filters").toArray();
    require(filters.size() >= 1 && filters.size() <= 16, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profiles need 1–16 correction filters."));
    for (const auto &value : filters) {
        require(value.isObject(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Invalid filter."));
        const auto f = value.toObject();
        for (auto it = f.begin(); it != f.end(); ++it)
            require(QStringList{"type", "frequency", "gain", "q"}.contains(it.key()), QT_TRANSLATE_NOOP("EquipmentProfiles", "Unknown filter field; import would lose data."));
        const auto type = f.value("type").toString();
        require(QStringList{"PK", "LS", "HS"}.contains(type), QT_TRANSLATE_NOOP("EquipmentProfiles", "Unsupported filter type."));
        require(f.value("frequency").isDouble() && f.value("gain").isDouble() &&
                    f.value("q").isDouble(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Filter values must be numbers."));
        EqBand b{f.value("frequency").toDouble(), f.value("gain").toDouble(),
                 f.value("q").toDouble(),
                 type == "LS"   ? FilterType::LowShelf
                 : type == "HS" ? FilterType::HighShelf
                                : FilterType::Peaking};
        require(std::isfinite(b.frequency) && std::isfinite(b.gainDb) && std::isfinite(b.q) &&
                    b.frequency >= 20 && b.frequency <= 20000 && std::abs(b.gainDb) <= 6 &&
                    b.q >= .1 && b.q <= 6, QT_TRANSLATE_NOOP("EquipmentProfiles", "Filters exceed frequency, gain or Q limits."));
        p.filters.append(b);
    }
    const auto response = o.value("response").toArray();
    require(response.size() <= 4096, QT_TRANSLATE_NOOP("EquipmentProfiles", "Response exceeds 4096 points."));
    for (const auto &v : response) {
        const auto a = v.toArray();
        require(a.size() == 2 && a[0].isDouble() && a[1].isDouble(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Invalid response point."));
        Point pt{a[0].toDouble(), a[1].toDouble()};
        require(std::isfinite(pt.frequency) && std::isfinite(pt.db) && pt.frequency >= 10 &&
                    pt.frequency <= 40000 && std::abs(pt.db) <= 200 &&
                    (p.response.isEmpty() || pt.frequency > p.response.back().frequency), QT_TRANSLATE_NOOP("EquipmentProfiles", "Response frequencies must increase, with finite bounded values."));
        p.response.append(pt);
    }
    if (p.id.isEmpty())
        p.id = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return p;
}
QVector<Point> parseResponseText(const QByteArray &bytes) {
    require(bytes.size() <= 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Response exceeds the 1 MiB limit."));
    require(soundcurrent::daw::validUtf8(
                std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))), QT_TRANSLATE_NOOP("EquipmentProfiles", "Response is not valid UTF-8."));
    QVector<Point> out;
    for (const auto &line : QString::fromUtf8(bytes).split('\n')) {
        const auto s = line.trimmed();
        if (s.isEmpty() || s.startsWith('*') || s.startsWith('#') || s.startsWith(';') ||
            s.startsWith("frequency", Qt::CaseInsensitive))
            continue;
        const auto tokens = s.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
        bool a = false, b = false;
        const double hz = tokens.value(0).toDouble(&a), db = tokens.value(1).toDouble(&b);
        require(a && b && (tokens.size() == 2 || tokens.size() == 3), QT_TRANSLATE_NOOP("EquipmentProfiles", "Expected frequency Hz and relative measured response dB on every data line."));
        require(out.size() < 4096 && std::isfinite(hz) && std::isfinite(db) && hz >= 10 &&
                    hz <= 40000 && std::abs(db) <= 200 &&
                    (out.isEmpty() || hz > out.back().frequency), QT_TRANSLATE_NOOP("EquipmentProfiles", "Invalid or unordered response data."));
        out.append({hz, db});
    }
    require(out.size() >= 2, QT_TRANSLATE_NOOP("EquipmentProfiles", "Response needs at least two measured points."));
    return out;
}
QVector<EqBand> fitResponse(const QVector<Point> &points) {
    require(points.size() >= 2 && points.size() <= 4096, QT_TRANSLATE_NOOP("EquipmentProfiles", "Response needs 2–4096 measured points."));
    double previous = 0;
    for (const auto &pt : points) {
        require(std::isfinite(pt.frequency) && std::isfinite(pt.db) && pt.frequency >= 10 &&
                    pt.frequency <= 40000 && std::abs(pt.db) <= 200 && pt.frequency > previous, QT_TRANSLATE_NOOP("EquipmentProfiles", "Invalid or unordered measured response."));
        previous = pt.frequency;
    }
    QVector<EqBand> bands;
    const double low = std::max(20., points.front().frequency),
                 high = std::min(20000., points.back().frequency);
    require(high > low, QT_TRANSLATE_NOOP("EquipmentProfiles", "Response has no usable audio range."));
    for (int i = 0; i < 16; ++i)
        bands.append({low * std::pow(high / low, (i + .5) / 16.), 0, 1.4});
    // Bounded coordinate descent against measured relative response; no extrapolation or phase
    // reconstruction.
    for (int iteration = 0; iteration < 8; ++iteration)
        for (auto &b : bands) {
            double numerator = 0, denominator = 0;
            const double old = b.gainDb;
            b.gainDb = 1;
            for (int i = 0; i < 128; ++i) {
                const double f = low * std::pow(high / low, i / 127.);
                const double basis = filterResponseDb(b, 48000, f);
                double current = 0;
                for (const auto &other : bands)
                    if (&other != &b)
                        current += filterResponseDb(other, 48000, f);
                numerator += basis * (-responseAt(points, f) - current);
                denominator += basis * basis;
            }
            b.gainDb = denominator > 0 ? std::clamp(numerator / denominator, -6., 6.) : old;
            if (b.frequency < 80 && b.gainDb > 0)
                b.gainDb = 0;
        }
    return bands;
}
QString libraryPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/equipment.json";
}
QVector<Profile> loadLibrary() {
    QFile f(libraryPath());
    if (!f.exists())
        return {};
    require(f.open(QIODevice::ReadOnly), QT_TRANSLATE_NOOP("EquipmentProfiles", "Cannot read profile library."));
    require(f.size() <= 16 * 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile library exceeds 16 MiB."));
    const auto libraryBytes = f.read(16 * 1024 * 1024 + 1);
    require(libraryBytes.size() <= 16 * 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile library exceeds 16 MiB."));
    const auto doc = QJsonDocument::fromJson(libraryBytes);
    require(doc.isArray() && doc.array().size() <= 256, QT_TRANSLATE_NOOP("EquipmentProfiles", "Invalid profile library."));
    QVector<Profile> out;
    QSet<QString> ids;
    for (const auto &v : doc.array()) {
        auto profile = parse(QJsonDocument(v.toObject()).toJson());
        require(!ids.contains(profile.id), QT_TRANSLATE_NOOP("EquipmentProfiles", "Duplicate profile identity in library."));
        ids.insert(profile.id);
        out.append(profile);
    }
    return out;
}
void saveLibrary(const QVector<Profile> &profiles) {
    require(profiles.size() <= 256, QT_TRANSLATE_NOOP("EquipmentProfiles", "The custom library holds up to 256 profiles."));
    QJsonArray a;
    QSet<QString> ids;
    for (const auto &p : profiles) {
        const auto o = serialize(p);
        const auto validated = parse(QJsonDocument(o).toJson());
        require(!ids.contains(validated.id), QT_TRANSLATE_NOOP("EquipmentProfiles", "Duplicate profile identity in library."));
        ids.insert(validated.id);
        a.append(o);
    }
    const auto bytes = QJsonDocument(a).toJson();
    require(bytes.size() <= 16 * 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Library exceeds 16 MiB."));
    require(QDir().mkpath(QFileInfo(libraryPath()).absolutePath()), QT_TRANSLATE_NOOP("EquipmentProfiles", "Cannot create profile folder."));
    QSaveFile f(libraryPath());
    require(f.open(QIODevice::WriteOnly), QT_TRANSLATE_NOOP("EquipmentProfiles", "Cannot save profile library."));
    require(f.write(bytes) == bytes.size() && f.commit(), QT_TRANSLATE_NOOP("EquipmentProfiles", "Cannot finish saving profile library."));
}
QVector<Profile> bundledProfiles() {
    initializeEquipmentResources();
    QVector<Profile> out;
    for (const auto &path : {":/daw-equipment/spinorama.json"}) {
        QFile f(path);
        require(f.open(QIODevice::ReadOnly), QT_TRANSLATE_NOOP("EquipmentProfiles", "Equipment resource missing."));
        for (const auto &v : QJsonDocument::fromJson(f.readAll()).array())
            out.append(parse(QJsonDocument(v.toObject()).toJson()));
    }
    return out;
}
void saveNewProfile(QWidget *parent, Profile p) {
    Editor editor(p, parent, [parent](Profile draft) {
        try {
            auto profiles = loadLibrary();
            profiles.append(draft);
            saveLibrary(profiles);
            return true;
        } catch (const std::exception &e) {
            QMessageBox::warning(parent, EquipmentText::tr("Save profile"), QString::fromUtf8(e.what()));
            return false;
        }
    });
    editor.dirty = true;
    fitWindow(&editor);
    editor.exec();
}
void openLibrary(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("equipmentLibrary");
    dialog.setWindowTitle(EquipmentText::tr("Equipment profiles — brand / family / model"));
    dialog.resize(800, 650);
    auto *layout = scrollLayout(&dialog);
    auto *notice = new QLabel(EquipmentText::tr("Edit and save equipment profiles here. Monitoring correction "
                                 "routing is not yet available."));
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::PlainText);
    layout->addWidget(notice);
    auto *search = new QLineEdit;
    search->setObjectName("equipmentSearch");
    search->setPlaceholderText(EquipmentText::tr("Search brand, family, model or measurement conditions"));
    layout->addWidget(search);
    auto *list = new FocusCombo;
    list->setObjectName("equipmentProfileList");
    list->setMaxVisibleItems(15);
    list->setAccessibleName(EquipmentText::tr("Equipment profiles by brand family and model"));
    layout->addWidget(list);
    auto *details = new QLabel;
    details->setWordWrap(true);
    details->setTextFormat(Qt::PlainText);
    layout->addWidget(details);
    layout->addWidget(new QLabel("Teal: correction EQ. Orange: measured response, when supplied. "
                                 "Vertical scale is relative dB."));
    auto *plot = new Plot;
    layout->addWidget(plot);
    QVector<Profile> custom = loadLibrary(), profiles = bundledProfiles();
    profiles += custom;
    auto *taxonomy = new QHBoxLayout;
    auto *kindFilter = new FocusCombo;
    kindFilter->setObjectName("equipmentKindFilter");
    kindFilter->addItem(EquipmentText::tr("All equipment"));
    kindFilter->addItem(EquipmentText::tr("Speakers"), "speaker");
    kindFilter->addItem(EquipmentText::tr("Microphones"), "microphone");
    kindFilter->addItem(EquipmentText::tr("Amplifiers / receivers"), "amplifier");
    kindFilter->addItem(EquipmentText::tr("Whole system"), "whole_system");
    kindFilter->setAccessibleName(EquipmentText::tr("Equipment type"));
    auto *brandFilter = new FocusCombo;
    brandFilter->setObjectName("equipmentBrandFilter");
    brandFilter->setAccessibleName(EquipmentText::tr("Equipment brand"));
    auto *familyFilter = new FocusCombo;
    familyFilter->setObjectName("equipmentFamilyFilter");
    familyFilter->setAccessibleName(EquipmentText::tr("Equipment family"));
    taxonomy->addWidget(kindFilter);
    taxonomy->addWidget(brandFilter);
    taxonomy->addWidget(familyFilter);
    layout->insertLayout(0, taxonomy);
    auto *classification = new QHBoxLayout;
    auto *subtypeFilter = new FocusCombo;
    subtypeFilter->setObjectName("equipmentSubtypeFilter");
    subtypeFilter->setAccessibleName(EquipmentText::tr("Equipment subtype"));
    auto *powerFilter = new FocusCombo;
    powerFilter->setObjectName("equipmentPowerFilter");
    powerFilter->setAccessibleName(EquipmentText::tr("Equipment power type"));
    classification->addWidget(subtypeFilter);
    classification->addWidget(powerFilter);
    layout->insertLayout(1, classification);
    auto taxonomyRefresh = [&] {
        const QSignalBlocker b(brandFilter), f(familyFilter), st(subtypeFilter), pw(powerFilter);
        const auto brand = brandFilter->currentText(), family = familyFilter->currentText();
        const auto subtype = subtypeFilter->currentText(), power = powerFilter->currentText();
        QStringList brands, families, subtypes, powers;
        for (const auto &p : profiles)
            if (kindFilter->currentIndex() == 0 || p.kind == kindFilter->currentData().toString()) {
                if (!subtypes.contains(p.equipmentType))
                    subtypes.append(p.equipmentType);
                if (!powers.contains(p.powerType))
                    powers.append(p.powerType);
                if (!brands.contains(p.brand))
                    brands.append(p.brand);
            }
        // A kind change can remove the selected brand. Build families for the effective
        // brand after that reset, rather than leaving the family list empty.
        const bool selectedBrand = brandFilter->currentIndex() > 0 && brands.contains(brand);
        for (const auto &p : profiles)
            if ((kindFilter->currentIndex() == 0 ||
                 p.kind == kindFilter->currentData().toString()) &&
                (!selectedBrand || p.brand == brand) && !families.contains(p.family))
                families.append(p.family);
        brands.sort(Qt::CaseInsensitive);
        families.sort(Qt::CaseInsensitive);
        subtypes.sort(Qt::CaseInsensitive);
        powers.sort(Qt::CaseInsensitive);
        brandFilter->clear();
        brandFilter->addItem(EquipmentText::tr("All brands"));
        brandFilter->addItems(brands);
        brandFilter->setCurrentIndex(std::max(0, brandFilter->findText(brand)));
        familyFilter->clear();
        familyFilter->addItem(EquipmentText::tr("All families"));
        familyFilter->addItems(families);
        familyFilter->setCurrentIndex(std::max(0, familyFilter->findText(family)));
        subtypeFilter->clear();
        subtypeFilter->addItem(EquipmentText::tr("All subtypes"));
        subtypeFilter->addItems(subtypes);
        subtypeFilter->setCurrentIndex(std::max(0, subtypeFilter->findText(subtype)));
        powerFilter->clear();
        powerFilter->addItem(EquipmentText::tr("All power types"));
        powerFilter->addItems(powers);
        powerFilter->setCurrentIndex(std::max(0, powerFilter->findText(power)));
    };
    taxonomyRefresh();
    auto refresh = [&] {
        list->clear();
        for (int i = 0; i < profiles.size(); ++i) {
            const auto &p = profiles[i];
            const QString name = p.kind + " / " + p.brand + " / " + p.family + " / " + p.model +
                                 (p.custom ? " [custom]" : "");
            if ((kindFilter->currentIndex() == 0 ||
                 p.kind == kindFilter->currentData().toString()) &&
                (brandFilter->currentIndex() == 0 || p.brand == brandFilter->currentText()) &&
                (familyFilter->currentIndex() == 0 || p.family == familyFilter->currentText()) &&
                (subtypeFilter->currentIndex() == 0 ||
                 p.equipmentType == subtypeFilter->currentText()) &&
                (powerFilter->currentIndex() == 0 || p.powerType == powerFilter->currentText()) &&
                (name + " " + p.conditions + " " + p.equipmentType + " " + p.powerType)
                    .contains(search->text(), Qt::CaseInsensitive))
                list->addItem(name, i);
        }
    };
    QObject::connect(list, &QComboBox::currentIndexChanged, &dialog, [&] {
        if (list->currentIndex() < 0) {
            details->clear();
            plot->profile = {};
            plot->update();
            return;
        }
        const auto &p = profiles[list->currentData().toInt()];
        details->setText(EquipmentText::tr("Subtype: %1; power: %2").arg(p.equipmentType, p.powerType) + "\n" +
                         p.source + "\n" + p.conditions + "\n" + p.provenance);
        plot->profile = p;
        plot->update();
    });
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&] { refresh(); });
    QObject::connect(kindFilter, &QComboBox::currentIndexChanged, &dialog, [&] {
        taxonomyRefresh();
        refresh();
    });
    QObject::connect(brandFilter, &QComboBox::currentIndexChanged, &dialog, [&] {
        taxonomyRefresh();
        refresh();
    });
    QObject::connect(familyFilter, &QComboBox::currentIndexChanged, &dialog, [&] { refresh(); });
    QObject::connect(subtypeFilter, &QComboBox::currentIndexChanged, &dialog, [&] { refresh(); });
    QObject::connect(powerFilter, &QComboBox::currentIndexChanged, &dialog, [&] { refresh(); });
    auto save = [&](Profile p) {
        try {
            const auto bytes = QJsonDocument(serialize(p)).toJson();
            p = parse(bytes);
            for (const auto &existing : profiles)
                require(existing.id != p.id, QT_TRANSLATE_NOOP("EquipmentProfiles", "Profile identity already exists; edit a custom copy instead."));
            auto proposed = custom;
            proposed.append(p);
            saveLibrary(proposed);
            custom = proposed;
            profiles = bundledProfiles();
            profiles += custom;
            taxonomyRefresh();
            {
                const QSignalBlocker a(kindFilter), b(brandFilter), c(familyFilter), d(search),
                    st(subtypeFilter), pw(powerFilter);
                kindFilter->setCurrentIndex(0);
                brandFilter->setCurrentIndex(0);
                familyFilter->setCurrentIndex(0);
                subtypeFilter->setCurrentIndex(0);
                powerFilter->setCurrentIndex(0);
                search->clear();
            }
            taxonomyRefresh();
            refresh();
            for (int i = 0; i < list->count(); ++i)
                if (profiles[list->itemData(i).toInt()].id == p.id) {
                    list->setCurrentIndex(i);
                    break;
                }
            return true;
        } catch (const std::exception &e) {
            QMessageBox::warning(&dialog, EquipmentText::tr("Profile"), QString::fromUtf8(e.what()));
            return false;
        }
    };
    auto *row = new QHBoxLayout;
    layout->addLayout(row);
    auto button = [&](const QString &label) {
        auto *b = new QPushButton(label);
        row->addWidget(b);
        return b;
    };
    auto *import = button(EquipmentText::tr("Import JSON"));
    auto *text = button(EquipmentText::tr("Import response text"));
    auto *create = button(EquipmentText::tr("Create profile"));
    auto *edit = button(EquipmentText::tr("Edit / save copy"));
    edit->setObjectName("editEquipmentProfile");
    auto *exportButton = button(EquipmentText::tr("Export JSON"));

    QObject::connect(import, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(&dialog, "Import equipment profile", {},
                                                       "Equipment profiles (*.json)");
        if (path.isEmpty())
            return;
        try {
            QFile f(path);
            require(f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Cannot read profile or file exceeds 1 MiB."));
            auto p = parse(f.read(1024 * 1024 + 1));
            if (ask(&dialog, "Import profile?",
                    p.brand + " / " + p.model + "\n" + p.conditions + "\nImport into your library?",
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
                save(p);
        } catch (const std::exception &e) {
            QMessageBox::warning(&dialog, EquipmentText::tr("Import"), e.what());
        }
    });
    QObject::connect(text, &QPushButton::clicked, &dialog, [&] {
        const auto path =
            QFileDialog::getOpenFileName(&dialog, "Import relative measured response", {},
                                         "Response data (*.txt *.csv *.frd *.cal)");
        if (path.isEmpty())
            return;
        try {
            QFile f(path);
            require(f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024, QT_TRANSLATE_NOOP("EquipmentProfiles", "Cannot read response or file exceeds 1 MiB."));
            Profile p;
            p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            p.kind = "microphone";
            p.brand = "Custom";
            p.family = "Measured response";
            p.model = QFileInfo(path).completeBaseName().left(120);
            p.custom = true;
            p.conditions =
                "User imported relative frequency response; specify microphone orientation / "
                "serial, or speaker measurement conditions before use.";
            p.provenance = "Imported " + QFileInfo(path).fileName() + "; SHA256 " +
                           QString::fromLatin1(QCryptographicHash::hash(f.peek(1024 * 1024 + 1),
                                                                        QCryptographicHash::Sha256)
                                                   .toHex());
            p.response = parseResponseText(f.read(1024 * 1024 + 1));
            p.filters = fitResponse(p.response);
            QDialog kindDialog(&dialog);
            auto *kl = new QVBoxLayout(&kindDialog);
            kl->addWidget(new QLabel(
                EquipmentText::tr("This imports measured RESPONSE, not already-inverted EQ gains. Confirm "
                   "equipment type. Absolute SPL needs normalization before import.")));
            auto *k = new FocusCombo;
            k->addItems({"microphone", "speaker", "amplifier", "whole_system"});
            kl->addWidget(k);
            auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            kl->addWidget(bb);
            QObject::connect(bb, &QDialogButtonBox::accepted, &kindDialog, &QDialog::accept);
            QObject::connect(bb, &QDialogButtonBox::rejected, &kindDialog, &QDialog::reject);
            if (kindDialog.exec() != QDialog::Accepted)
                return;
            p.kind = k->currentText();
            Editor editor(p, &dialog, save);
            editor.dirty = true;
            fitWindow(&editor);
            editor.exec();
        } catch (const std::exception &e) {
            QMessageBox::warning(&dialog, EquipmentText::tr("Response import"), e.what());
        }
    });
    QObject::connect(create, &QPushButton::clicked, &dialog, [&] {
        Profile p;
        p.kind = "speaker";
        p.brand = "Custom";
        p.family = "My equipment";
        p.model = "New profile";
        p.custom = true;
        p.conditions = "User-created correction; enter equipment and measurement conditions.";
        p.filters.append({1000, 0, 1});
        QDialog select(&dialog);
        auto *l = new QVBoxLayout(&select);
        auto *k = new FocusCombo;
        k->addItems({"speaker", "microphone", "amplifier", "whole_system"});
        l->addWidget(k);
        auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        l->addWidget(b);
        QObject::connect(b, &QDialogButtonBox::accepted, &select, &QDialog::accept);
        QObject::connect(b, &QDialogButtonBox::rejected, &select, &QDialog::reject);
        if (select.exec() != QDialog::Accepted)
            return;
        p.kind = k->currentText();
        Editor editor(p, &dialog, save);
        editor.dirty = true;
        fitWindow(&editor);
        editor.exec();
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        if (list->currentIndex() < 0)
            return;
        Editor editor(profiles[list->currentData().toInt()], &dialog, save);
        fitWindow(&editor);
        editor.exec();
    });
    QObject::connect(exportButton, &QPushButton::clicked, &dialog, [&] {
        if (list->currentIndex() < 0)
            return;
        const auto path = QFileDialog::getSaveFileName(&dialog, "Export profile", {},
                                                       "Equipment profile (*.json)");
        if (path.isEmpty())
            return;
        QSaveFile f(path);
        const auto bytes = QJsonDocument(serialize(profiles[list->currentData().toInt()])).toJson();
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
            QMessageBox::warning(&dialog, EquipmentText::tr("Export"), "Cannot save profile.");
    });
    auto *sources = new QLabel(
        "Published measurement sources: <a href=\"https://www.spinorama.org/\">Speaker "
        "measurements / EQ</a> "
        "· <a href=\"https://support.daytonaudio.com/microphonecalibrationtool\">Dayton serial "
        "calibration</a> · <a "
        "href=\"https://www.minidsp.com/products/acoustic-measurement/umik-1\">miniDSP "
        "serial calibration</a> · <a href=\"https://www.neumann.com/de-de/downloads/\">Neumann "
        "microphone "
        "graphs</a> · <a href=\"https://docs.audio-technica.com/us/at2020_english.pdf\">AT2020 "
        "response "
        "graph</a> · <a "
        "href=\"https://www.soundstagenetwork.com/"
        "index.php?Itemid=154&amp;id=97&amp;option=com_content&amp;view=category\">Amplifier "
        "measurements</a>");
    sources->setWordWrap(true);
    sources->setOpenExternalLinks(true);
    layout->addWidget(sources);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    static_cast<QVBoxLayout *>(dialog.layout())->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    fitWindow(&dialog);
    dialog.exec();
}
} // namespace soundcurrent::daw::equipment
