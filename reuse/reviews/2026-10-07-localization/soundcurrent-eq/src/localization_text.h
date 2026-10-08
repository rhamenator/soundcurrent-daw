// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QCoreApplication>
namespace soundcurrent::i18n {
inline QString text(const char *source, int n = -1) {
    return QCoreApplication::translate("SoundCurrent", source, nullptr, n);
}
}
#define SC_TR(source) ::soundcurrent::i18n::text(source)
