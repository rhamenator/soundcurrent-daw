// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <soundcurrent/session.hpp>
#include <algorithm>
#include <memory>
namespace soundcurrent::daw::ui {
// Immutable UI-only borrow; caller retains the source snapshot through use.
inline const Track *trackForId(const Session *s, const std::optional<Id> &id) {
    if (!s || s->tracks.empty())
        return nullptr;
    if (!id)
        return &s->tracks.front();
    const auto it = std::find_if(s->tracks.begin(), s->tracks.end(),
                                 [&](const auto &t) { return t.id == *id; });
    return it == s->tracks.end() ? nullptr : &*it;
}
// UI/control-only projection for the current single-track adapters. Canonical
// ordering is never mutated or saved. Missing identity must not fall back silently.
inline std::shared_ptr<const Session> sessionForTrack(std::shared_ptr<const Session> s,
                                                      const std::optional<Id> &id) {
    if (!s || s->tracks.empty())
        return {};
    if (!id || s->tracks.front().id == *id)
        return s;
    const auto found = std::find_if(s->tracks.begin(), s->tracks.end(),
                                    [&](const auto &t) { return t.id == *id; });
    if (found == s->tracks.end())
        return {};
    auto projected = std::make_shared<Session>(*s);
    const auto index = std::size_t(found - s->tracks.begin());
    std::rotate(projected->tracks.begin(), projected->tracks.begin() + index,
                projected->tracks.begin() + index + 1);
    return projected;
}
} // namespace soundcurrent::daw::ui
