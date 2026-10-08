// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QCoreApplication>
namespace soundcurrent::i18n {
inline QString text(const char *source, int n = -1) {
    return QCoreApplication::translate("SoundCurrent", source, nullptr, n);
}
// Keep a signed number and its invariant unit together inside RTL prose.
// Directional isolates are introduced only at display time, never in catalogs
// or persisted parameter values. The translated sentence keeps its own order.
inline QString numberWithUnit(QString pattern, const QString &number, const QString &unit) {
    const QString token = QStringLiteral("%1 ") + unit;
    pattern.replace(token, QString(QChar(0x2066)) + token + QChar(0x2069));
    return pattern.arg(number);
}
}
#define SC_TR(source) ::soundcurrent::i18n::text(source)
