// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "dsp.h"
#include <QDialog>
#include <QJsonObject>
#include <QVector>
#include <functional>
namespace soundcurrent::equipment {
struct Point {
    double frequency, db;
};
struct Profile {
    QString id, kind, brand, family, model, source, conditions, provenance;
    QString equipmentType = "Unclassified", powerType = "Unknown";
    QVector<EqBand> filters;
    QVector<Point> response;
    bool custom = false;
};
QJsonObject serialize(const Profile &);
Profile parse(const QByteArray &); // throws with a user-readable reason; bounded JSON or response text
QVector<EqBand> fitResponse(const QVector<Point> &);
QString libraryPath();
QVector<Profile> loadLibrary();
void saveLibrary(const QVector<Profile> &);
QVector<Profile> bundledProfiles();
void saveNewProfile(QWidget *, Profile);
void openLibrary(QWidget *, const std::function<void(const Profile &)> &apply);
} // namespace soundcurrent::equipment
