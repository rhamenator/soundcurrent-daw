// SPDX-License-Identifier: GPL-3.0-only
// Modified 2026-10-05 from the pinned equalizer snapshot; see reuse/equipment/provenance.json.
#pragma once
#include "equipment_curve.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QVector>
#include <functional>
namespace soundcurrent::daw::equipment {
struct Point {
    double frequency, db;
    bool operator==(const Point &) const = default;
};
struct Profile {
    QString id, kind, brand, family, model, source, conditions, provenance;
    QString equipmentType = "Unclassified", powerType = "Unknown";
    QVector<EqBand> filters;
    QVector<Point> response;
    bool custom = false;
    bool operator==(const Profile &) const = default;
};
QJsonObject serialize(const Profile &);
Profile
parse(const QByteArray &); // throws with a user-readable reason; bounded JSON or response text
QVector<Point> parseResponseText(const QByteArray &); // Hz and relative dB; optional phase ignored
QVector<EqBand> fitResponse(const QVector<Point> &);
QString libraryPath();
QVector<Profile> loadLibrary();
void saveLibrary(const QVector<Profile> &);
QVector<Profile> bundledProfiles();
void saveNewProfile(QWidget *, Profile);
void openLibrary(QWidget *); // Offline library/editor; monitoring integration is a separate task.
} // namespace soundcurrent::daw::equipment
