// SPDX-License-Identifier: GPL-3.0-only
#include "accelerating_spinbox.h"
#include "equipment_profiles.h"
#include <QApplication>
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
namespace soundcurrent::equipment {
namespace {
void require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
int ask(QWidget *parent, const QString &title, const QString &text, QMessageBox::StandardButtons buttons,
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
            return a.db + (b.db - a.db) * std::log(f / a.frequency) / std::log(b.frequency / a.frequency);
        }
    return points.back().db;
}
class Plot : public QWidget {
  public:
    Profile profile;
    std::function<void(int, double, double)> onDrag;
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
        setMinimumHeight(180);
        setAccessibleName("Published response and editable correction curves");
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (!onDrag || e->button() != Qt::LeftButton)
            return;
        const QRectF area(45, 15, width() - 90, height() - 40);
        double best = 20;
        for (int i = 0; i < profile.filters.size(); ++i) {
            const auto &b = profile.filters[i];
            const QPointF point(area.left() + std::log(b.frequency / 20.) / std::log(1000.) * area.width(),
                                area.center().y() - b.gainDb / (2 * rangeDb()) * area.height());
            const auto delta = point - e->position();
            const double distance = std::hypot(delta.x(), delta.y());
            if (distance < best) {
                best = distance;
                dragging = i;
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!onDrag || dragging < 0)
            return;
        const QRectF area(45, 15, width() - 90, height() - 40);
        const double f =
            20. * std::pow(1000., std::clamp((e->position().x() - area.left()) / area.width(), 0., 1.));
        const double gain =
            std::clamp((area.center().y() - e->position().y()) / area.height() * (2 * rangeDb()), -6., 6.);
        onDrag(dragging, f, gain);
    }
    void mouseReleaseEvent(QMouseEvent *) override { dragging = -1; }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#172337"));
        const QRectF area(45, 15, width() - 90, height() - 40);
        auto x = [&](double f) { return area.left() + std::log(f / 20.) / std::log(1000.) * area.width(); };
        auto y = [&](double d) {
            return area.center().y() - std::clamp(d, -rangeDb(), rangeDb()) / (2 * rangeDb()) * area.height();
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
        auto draw = [&](QColor color, auto db) {
            QPainterPath path;
            for (int i = 0; i <= 400; ++i) {
                const double f = 20. * std::pow(1000., i / 400.);
                const QPointF v(x(f), y(db(f)));
                if (i)
                    path.lineTo(v);
                else
                    path.moveTo(v);
            }
            p.setPen(QPen(color, 2));
            p.drawPath(path);
        };
        if (!profile.response.isEmpty())
            draw(QColor("#f6ad55"), [&](double f) { return responseAt(profile.response, f); });
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
    bool dirty = false;
    QString originalId, originalProvenance;
    QTableWidget *table;
    Plot *plot;
    QLineEdit *brand, *family, *model, *source, *conditions, *equipmentType, *powerType;
    std::function<bool(Profile)> save;
    Editor(Profile profile, QWidget *parent, std::function<bool(Profile)> writer)
        : QDialog(parent), draft(std::move(profile)), save(std::move(writer)) {
        originalId = draft.id;
        originalProvenance = draft.provenance;
        setWindowTitle("Equipment profile editor");
        resize(760, 650);
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;
        auto field = [&](const QString &label, const QString &value) {
            auto *e = new QLineEdit(value);
            e->setMaxLength(label == "Source" ? 2048 : label == "Conditions" ? 2000 : 120);
            e->setCursorPosition(0);
            form->addRow(label, e);
            QObject::connect(e, &QLineEdit::textEdited, this, [this] { dirty = true; });
            return e;
        };
        brand = field("Brand", draft.brand);
        family = field("Family", draft.family);
        equipmentType = field("Equipment subtype", draft.equipmentType);
        powerType = field("Active / passive / unknown", draft.powerType);
        model = field("Model", draft.model);
        source = field("Source", draft.source);
        conditions = field("Conditions", draft.conditions);
        layout->addLayout(form);
        auto *legend = new QLabel(
            "Orange: measured response where supplied. Teal: correction at 48 kHz. Drag teal control points "
            "or edit the table. Saving preserves the reference and creates a custom copy.");
        legend->setWordWrap(true);
        layout->addWidget(legend);
        plot = new Plot;
        plot->profile = draft;
        layout->addWidget(plot);
        table = new QTableWidget(draft.filters.size(), 4);
        table->setHorizontalHeaderLabels({"Type", "Frequency Hz", "Gain dB", "Q"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        layout->addWidget(table);
        for (int i = 0; i < draft.filters.size(); ++i)
            addRow(i, draft.filters[i]);
        plot->onDrag = [this](int r, double f, double g) {
            if (r >= table->rowCount())
                return;
            qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 1))->setValue(f);
            qobject_cast<QDoubleSpinBox *>(table->cellWidget(r, 2))->setValue(g);
        };
        auto *row = new QHBoxLayout;
        auto *add = new QPushButton("Add filter");
        auto *remove = new QPushButton("Remove selected filter");
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
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            if (persist())
                accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &Editor::reject);
    }
    void addRow(int r, const EqBand &b) {
        auto *type = new QComboBox;
        type->addItems({"PK", "LS", "HS"});
        type->setCurrentText(typeName(b.type));
        table->setCellWidget(r, 0, type);
        QObject::connect(type, &QComboBox::currentIndexChanged, this, [this] { changed(); });
        for (int c = 1; c < 4; ++c) {
            auto *spin = new soundcurrent::AcceleratingDoubleSpinBox;
            spin->setDecimals(c == 1 ? 1 : 2);
            spin->setRange(c == 1 ? 20 : c == 2 ? -6 : .1, c == 1 ? 20000 : c == 2 ? 6 : 6);
            spin->setValue(c == 1 ? b.frequency : c == 2 ? b.gainDb : b.q);
            spin->setAccessibleName(table->horizontalHeaderItem(c)->text());
            table->setCellWidget(r, c, spin);
            QObject::connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { changed(); });
        }
    }
    void changed() {
        dirty = true;
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
            const auto answer =
                ask(this, "Save modified profile?",
                    "This profile has changed. Save a custom copy before leaving?",
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
        filters.append(QJsonObject{
            {"type", typeName(b.type)}, {"frequency", b.frequency}, {"gain", b.gainDb}, {"q", b.q}});
    for (const auto &v : p.response)
        response.append(QJsonArray{v.frequency, v.db});
    return {{"schema", 2},
            {"id", p.id},
            {"kind", p.kind},
            {"brand", p.brand},
            {"family", p.family},
            {"equipmentType", p.equipmentType}, {"powerType", p.powerType},
            {"model", p.model},
            {"measurementSource", p.source},
            {"conditions", p.conditions},
            {"provenance", p.provenance},
            {"custom", p.custom},
            {"filters", filters},
            {"response", response}};
}
Profile parse(const QByteArray &bytes) {
    require(bytes.size() <= 1024 * 1024, "Profile exceeds the 1 MiB limit.");
    const auto document = QJsonDocument::fromJson(bytes);
    require(document.isObject(),
            "Expected a JSON equipment profile. Import response text using the response import button.");
    const auto o = document.object();
    require(o.value("schema").toInt() == 2, "Unsupported equipment profile schema (expected 2).");
    Profile p;
    p.id = o.value("id").toString();
    p.kind = o.value("kind").toString();
    p.brand = o.value("brand").toString().trimmed();
    p.family = o.value("family").toString().trimmed();
    p.equipmentType = o.value("equipmentType").toString("Unclassified").trimmed();
    p.powerType = o.value("powerType").toString("Unknown").trimmed();
    require(!p.equipmentType.isEmpty() && p.equipmentType.size()<=120 && !p.powerType.isEmpty() && p.powerType.size()<=120,"Invalid equipment subtype or power type");
    p.model = o.value("model").toString().trimmed();
    p.source = o.value("measurementSource").toString();
    p.conditions = o.value("conditions").toString();
    p.provenance = o.value("provenance").toString();
    p.custom = o.value("custom").toBool();
    require(QStringList{"speaker", "microphone", "amplifier"}.contains(p.kind),
            "Equipment kind must be speaker, microphone or amplifier.");
    require(!p.brand.isEmpty() && !p.family.isEmpty() && !p.model.isEmpty() && p.brand.size() <= 120 &&
                p.family.size() <= 120 && p.model.size() <= 120,
            "Brand, family and model are required (maximum 120 characters each).");
    require(p.id.size() <= 120 && p.conditions.size() <= 2000 && p.provenance.size() <= 2000 &&
                p.source.size() <= 2048,
            "Profile metadata is too long.");
    require(!p.conditions.isEmpty(), "Measurement conditions are required.");
    const QUrl url(p.source);
    require(p.custom || (url.isValid() && url.scheme() == "https" && !url.host().isEmpty()),
            "Published profiles need an HTTPS measurement source.");
    const auto filters = o.value("filters").toArray();
    require(filters.size() >= 1 && filters.size() <= 16, "Profiles need 1–16 correction filters.");
    for (const auto &value : filters) {
        require(value.isObject(), "Invalid filter.");
        const auto f = value.toObject();
        const auto type = f.value("type").toString();
        require(QStringList{"PK", "LS", "HS"}.contains(type), "Unsupported filter type.");
        require(f.value("frequency").isDouble() && f.value("gain").isDouble() && f.value("q").isDouble(),
                "Filter values must be numbers.");
        EqBand b{f.value("frequency").toDouble(), f.value("gain").toDouble(), f.value("q").toDouble(),
                 type == "LS"   ? FilterType::LowShelf
                 : type == "HS" ? FilterType::HighShelf
                                : FilterType::Peaking};
        require(std::isfinite(b.frequency) && std::isfinite(b.gainDb) && std::isfinite(b.q) &&
                    b.frequency >= 20 && b.frequency <= 20000 && std::abs(b.gainDb) <= 6 && b.q >= .1 &&
                    b.q <= 6,
                "Filters exceed frequency, gain or Q limits.");
        p.filters.append(b);
    }
    const auto response = o.value("response").toArray();
    require(response.size() <= 4096, "Response exceeds 4096 points.");
    for (const auto &v : response) {
        const auto a = v.toArray();
        require(a.size() == 2 && a[0].isDouble() && a[1].isDouble(), "Invalid response point.");
        Point pt{a[0].toDouble(), a[1].toDouble()};
        require(std::isfinite(pt.frequency) && std::isfinite(pt.db) && pt.frequency >= 10 &&
                    pt.frequency <= 40000 && std::abs(pt.db) <= 200 &&
                    (p.response.isEmpty() || pt.frequency > p.response.back().frequency),
                "Response frequencies must increase, with finite bounded values.");
        p.response.append(pt);
    }
    if (p.id.isEmpty())
        p.id = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return p;
}
QVector<EqBand> fitResponse(const QVector<Point> &points) {
    require(points.size() >= 2 && points.size() <= 4096, "Response needs 2–4096 measured points.");
    double previous = 0;
    for (const auto &pt : points) {
        require(std::isfinite(pt.frequency) && std::isfinite(pt.db) && pt.frequency >= 10 &&
                    pt.frequency <= 40000 && std::abs(pt.db) <= 200 && pt.frequency > previous,
                "Invalid or unordered measured response.");
        previous = pt.frequency;
    }
    QVector<EqBand> bands;
    const double low = std::max(20., points.front().frequency),
                 high = std::min(20000., points.back().frequency);
    require(high > low, "Response has no usable audio range.");
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
    return qApp->arguments().contains("--ui-self-test")
               ? QFileInfo(QSettings().fileName()).absolutePath() + "/equipment.json"
               : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/equipment.json";
}
QVector<Profile> loadLibrary() {
    QFile f(libraryPath());
    if (!f.exists())
        return {};
    require(f.open(QIODevice::ReadOnly), "Cannot read profile library.");
    require(f.size() <= 16 * 1024 * 1024, "Profile library exceeds 16 MiB.");
    const auto doc = QJsonDocument::fromJson(f.readAll());
    require(doc.isArray() && doc.array().size() <= 256, "Invalid profile library.");
    QVector<Profile> out;
    for (const auto &v : doc.array())
        out.append(parse(QJsonDocument(v.toObject()).toJson()));
    return out;
}
void saveLibrary(const QVector<Profile> &profiles) {
    require(profiles.size() <= 256, "The custom library holds up to 256 profiles.");
    QJsonArray a;
    for (const auto &p : profiles) {
        const auto o = serialize(p);
        parse(QJsonDocument(o).toJson());
        a.append(o);
    }
    const auto bytes = QJsonDocument(a).toJson();
    require(bytes.size() <= 16 * 1024 * 1024, "Library exceeds 16 MiB.");
    require(QDir().mkpath(QFileInfo(libraryPath()).absolutePath()), "Cannot create profile folder.");
    QSaveFile f(libraryPath());
    require(f.open(QIODevice::WriteOnly), "Cannot save profile library.");
    require(f.write(bytes) == bytes.size() && f.commit(), "Cannot finish saving profile library.");
}
QVector<Profile> bundledProfiles() {
    QVector<Profile> out;
    for (const auto &path : {":/equipment/profiles.json", ":/equipment/spinorama.json"}) {
        QFile f(path);
        require(f.open(QIODevice::ReadOnly), "Equipment resource missing.");
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
            QMessageBox::warning(parent, "Save profile", e.what());
            return false;
        }
    });
    editor.dirty = true;
    editor.exec();
}
void openLibrary(QWidget *parent, const std::function<void(const Profile &)> &apply) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Equipment profiles — brand / family / model");
    dialog.resize(800, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search brand, family, model or measurement conditions");
    layout->addWidget(search);
    auto *list = new QComboBox;
    list->setMaxVisibleItems(15);
    list->setAccessibleName("Equipment profiles by brand family and model");
    layout->addWidget(list);
    auto *details = new QLabel;
    details->setWordWrap(true);
    details->setTextFormat(Qt::PlainText);
    layout->addWidget(details);
    layout->addWidget(new QLabel(
        "Teal: correction EQ. Orange: measured response, when supplied. Vertical scale is relative dB."));
    auto *plot = new Plot;
    layout->addWidget(plot);
    QVector<Profile> custom = loadLibrary(), profiles = bundledProfiles();
    profiles += custom;
    auto *taxonomy = new QHBoxLayout;
    auto *kindFilter = new QComboBox;
    kindFilter->addItems({"All equipment", "speaker", "microphone", "amplifier"});
    kindFilter->setAccessibleName("Equipment type");
    auto *brandFilter = new QComboBox;
    brandFilter->setAccessibleName("Equipment brand");
    auto *familyFilter = new QComboBox;
    familyFilter->setAccessibleName("Equipment family");
    taxonomy->addWidget(kindFilter);
    taxonomy->addWidget(brandFilter);
    taxonomy->addWidget(familyFilter);
    auto *subtypeFilter=new QComboBox;subtypeFilter->setAccessibleName("Equipment subtype");taxonomy->addWidget(subtypeFilter);
    layout->insertLayout(0, taxonomy);
    auto taxonomyRefresh = [&] {
        const QSignalBlocker b(brandFilter), f(familyFilter), st(subtypeFilter);
        const auto brand = brandFilter->currentText(), family = familyFilter->currentText();
        QStringList brands, families, subtypes;
        for (const auto &p : profiles)
            if (kindFilter->currentIndex() == 0 || p.kind == kindFilter->currentText()) {
                if (!subtypes.contains(p.equipmentType))subtypes.append(p.equipmentType);
                if (!brands.contains(p.brand))
                    brands.append(p.brand);
                if ((brand == p.brand || brand == "All brands" || brand.isEmpty()) &&
                    !families.contains(p.family))
                    families.append(p.family);
            }
        const auto subtype=subtypeFilter->currentText();subtypes.sort(Qt::CaseInsensitive);subtypeFilter->clear();subtypeFilter->addItem("All subtypes");subtypeFilter->addItems(subtypes);subtypeFilter->setCurrentIndex(std::max(0,subtypeFilter->findText(subtype)));
        brands.sort(Qt::CaseInsensitive);
        families.sort(Qt::CaseInsensitive);
        brandFilter->clear();
        brandFilter->addItem("All brands");
        brandFilter->addItems(brands);
        brandFilter->setCurrentIndex(std::max(0, brandFilter->findText(brand)));
        familyFilter->clear();
        familyFilter->addItem("All families");
        familyFilter->addItems(families);
        familyFilter->setCurrentIndex(std::max(0, familyFilter->findText(family)));
    };
    taxonomyRefresh();
    auto refresh = [&] {
        list->clear();
        for (int i = 0; i < profiles.size(); ++i) {
            const auto &p = profiles[i];
            const QString name =
                p.kind + " / " + p.brand + " / " + p.family + " / " + p.model + (p.custom ? " [custom]" : "");
            if ((kindFilter->currentIndex() == 0 || p.kind == kindFilter->currentText()) &&
                (brandFilter->currentIndex() == 0 || p.brand == brandFilter->currentText()) &&
                (familyFilter->currentIndex() == 0 || p.family == familyFilter->currentText()) &&
                (subtypeFilter->currentIndex()==0 || p.equipmentType==subtypeFilter->currentText()) &&
                (name + " " + p.conditions).contains(search->text(), Qt::CaseInsensitive))
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
        details->setText(p.source + "\n" + p.conditions + "\n" + p.provenance);
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
    QObject::connect(subtypeFilter,&QComboBox::currentIndexChanged,&dialog,[&]{refresh();});
    auto save = [&](Profile p) {
        try {
            const auto bytes = QJsonDocument(serialize(p)).toJson();
            p = parse(bytes);
            auto proposed = custom;
            proposed.append(p);
            saveLibrary(proposed);
            custom = proposed;
            profiles = bundledProfiles();
            profiles += custom;
            taxonomyRefresh();
            {
                const QSignalBlocker a(kindFilter), b(brandFilter), c(familyFilter), d(search), st(subtypeFilter);
                kindFilter->setCurrentIndex(0);
                brandFilter->setCurrentIndex(0);
                familyFilter->setCurrentIndex(0);
                subtypeFilter->setCurrentIndex(0);
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
            QMessageBox::warning(&dialog, "Profile", QString::fromUtf8(e.what()));
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
    auto *import = button("Import JSON");
    auto *text = button("Import response text");
    auto *create = button("Create profile");
    auto *edit = button("Edit / save copy");
    auto *exportButton = button("Export JSON");
    auto *use = button("Apply profile");
    QObject::connect(import, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(&dialog, "Import equipment profile", {},
                                                       "Equipment profiles (*.json)");
        if (path.isEmpty())
            return;
        try {
            QFile f(path);
            require(f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024,
                    "Cannot read profile or file exceeds 1 MiB.");
            auto p = parse(f.readAll());
            if (ask(&dialog, "Import profile?",
                    p.brand + " / " + p.model + "\n" + p.conditions + "\nImport into your library?",
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
                save(p);
        } catch (const std::exception &e) {
            QMessageBox::warning(&dialog, "Import", e.what());
        }
    });
    QObject::connect(text, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(&dialog, "Import relative measured response", {},
                                                       "Response data (*.txt *.csv *.frd *.cal)");
        if (path.isEmpty())
            return;
        try {
            QFile f(path);
            require(f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024,
                    "Cannot read response or file exceeds 1 MiB.");
            Profile p;
            p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            p.kind = "microphone";
            p.brand = "Custom";
            p.family = "Measured response";
            p.model = QFileInfo(path).completeBaseName().left(120);
            p.custom = true;
            p.conditions = "User imported relative frequency response; specify microphone orientation / "
                           "serial, or speaker measurement conditions before use.";
            p.provenance =
                "Imported " + QFileInfo(path).fileName() + "; SHA256 " +
                QString::fromLatin1(
                    QCryptographicHash::hash(f.peek(f.size()), QCryptographicHash::Sha256).toHex());
            for (const auto &line : QString::fromUtf8(f.readAll()).split('\n')) {
                const auto s = line.trimmed();
                if (s.isEmpty() || s.startsWith('*') || s.startsWith('#') || s.startsWith(';') ||
                    s.startsWith("frequency", Qt::CaseInsensitive))
                    continue;
                const auto tokens = s.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
                bool a = false, b = false;
                const double hz = tokens.value(0).toDouble(&a), db = tokens.value(1).toDouble(&b);
                require(a && b && tokens.size() >= 2,
                        "Expected frequency Hz and relative measured response dB on every data line.");
                require(p.response.size() < 4096 && std::isfinite(hz) && std::isfinite(db) && hz >= 10 &&
                            hz <= 40000 && std::abs(db) <= 200 &&
                            (p.response.isEmpty() || hz > p.response.back().frequency),
                        "Invalid or unordered response data.");
                p.response.append({hz, db});
            }
            p.filters = fitResponse(p.response);
            QDialog kindDialog(&dialog);
            auto *kl = new QVBoxLayout(&kindDialog);
            kl->addWidget(new QLabel("This imports measured RESPONSE, not already-inverted EQ gains. Confirm "
                                     "equipment type. Absolute SPL needs normalization before import."));
            auto *k = new QComboBox;
            k->addItems({"microphone", "speaker", "amplifier"});
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
            editor.exec();
        } catch (const std::exception &e) {
            QMessageBox::warning(&dialog, "Response import", e.what());
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
        auto *k = new QComboBox;
        k->addItems({"speaker", "microphone", "amplifier"});
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
        editor.exec();
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        if (list->currentIndex() < 0)
            return;
        Editor editor(profiles[list->currentData().toInt()], &dialog, save);
        editor.exec();
    });
    QObject::connect(exportButton, &QPushButton::clicked, &dialog, [&] {
        if (list->currentIndex() < 0)
            return;
        const auto path =
            QFileDialog::getSaveFileName(&dialog, "Export profile", {}, "Equipment profile (*.json)");
        if (path.isEmpty())
            return;
        QSaveFile f(path);
        const auto bytes = QJsonDocument(serialize(profiles[list->currentData().toInt()])).toJson();
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
            QMessageBox::warning(&dialog, "Export", "Cannot save profile.");
    });
    QObject::connect(use, &QPushButton::clicked, &dialog, [&] {
        if (list->currentIndex() < 0)
            return;
        const auto p = profiles[list->currentData().toInt()];
        if (ask(&dialog, "Apply correction?",
                p.brand + " / " + p.model + "\n" + p.conditions + "\nApply this correction to the " + p.kind +
                    " route?",
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            apply(p);
        }
    });
    auto *sources = new QLabel(
        "Published measurement sources: <a href=\"https://www.spinorama.org/\">Speaker measurements / EQ</a> "
        "· <a href=\"https://support.daytonaudio.com/microphonecalibrationtool\">Dayton serial "
        "calibration</a> · <a href=\"https://www.minidsp.com/products/acoustic-measurement/umik-1\">miniDSP "
        "serial calibration</a> · <a href=\"https://www.neumann.com/de-de/downloads/\">Neumann microphone "
        "graphs</a> · <a href=\"https://docs.audio-technica.com/us/at2020_english.pdf\">AT2020 response "
        "graph</a> · <a "
        "href=\"https://www.soundstagenetwork.com/"
        "index.php?Itemid=154&amp;id=97&amp;option=com_content&amp;view=category\">Amplifier "
        "measurements</a>");
    sources->setWordWrap(true);
    sources->setOpenExternalLinks(true);
    layout->addWidget(sources);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    dialog.exec();
}
} // namespace soundcurrent::equipment
