// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/routing.hpp>
namespace soundcurrent::daw {
RouteMatch matchRouteIntent(const RouteIntent &r, std::size_t channel, std::string_view backend,
                            std::span<const ChannelPortIntent> available) {
    if (!r.backendId.empty() && r.backendId != backend)
        return {RouteMatchStatus::UnsupportedBackend, {}};
    if (r.ports.empty())
        return {r.portIdentity.empty() ? RouteMatchStatus::Unassigned : RouteMatchStatus::Legacy,
                {}};
    if (channel >= r.ports.size())
        throw ProjectError(ErrorCode::InvalidParameter, "Route channel outside intent");
    if (!r.ports[channel])
        return {};
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < available.size(); ++i)
        if (available[i] == *r.ports[channel]) {
            if (found)
                return {RouteMatchStatus::Ambiguous, {}};
            found = i;
        }
    return {found ? RouteMatchStatus::Found : RouteMatchStatus::Missing, found};
}
} // namespace soundcurrent::daw
