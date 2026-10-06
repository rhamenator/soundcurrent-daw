// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
#include <span>
namespace soundcurrent::daw {
enum class RouteMatchStatus { Unassigned, Legacy, UnsupportedBackend, Missing, Ambiguous, Found };
struct RouteMatch {
    RouteMatchStatus status = RouteMatchStatus::Unassigned;
    std::optional<std::size_t> index;
};
// Control-side descriptor resolution; never connects or picks a default endpoint.
RouteMatch matchRouteIntent(const RouteIntent &, std::size_t channel, std::string_view backend,
                            std::span<const ChannelPortIntent> available);
} // namespace soundcurrent::daw
