// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/session.hpp>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace soundcurrent::daw {
namespace {
void require(bool good, const char *message, ErrorCode code = ErrorCode::InvalidState) {
    if (!good)
        throw ProjectError(code, message);
}
template <class T> auto find(std::vector<T> &objects, const Id &id) {
    auto it =
        std::find_if(objects.begin(), objects.end(), [&](const auto &o) { return o.id == id; });
    require(it != objects.end(), "Edit object no longer exists", ErrorCode::InvalidId);
    return it;
}
template <class T> auto position(std::vector<T> &objects, const std::optional<Id> &before) {
    return before ? find(objects, *before) : objects.end();
}
Track &track(Session &s, const Id &id) {
    return *find(s.tracks, id);
}
template <class T> std::vector<Id> order(const std::vector<T> &objects) {
    std::vector<Id> ids;
    ids.reserve(objects.size());
    for (const auto &o : objects)
        ids.push_back(o.id);
    return ids;
}
template <class T, class P>
void differences(const std::vector<T> &before, const std::vector<T> &after, P &patches) {
    for (const auto &old : before) {
        const auto now =
            std::find_if(after.begin(), after.end(), [&](const auto &o) { return o.id == old.id; });
        if (now == after.end())
            patches.push_back({old.id, old, std::nullopt});
        else if (*now != old)
            patches.push_back({old.id, old, *now});
    }
    for (const auto &now : after)
        if (std::none_of(before.begin(), before.end(),
                         [&](const auto &o) { return o.id == now.id; }))
            patches.push_back({now.id, std::nullopt, now});
}
template <class T, class P>
void overlay(std::vector<T> &objects, const P &patches, const std::vector<Id> &beforeOrder,
             const std::vector<Id> &afterOrder, bool forward) {
    const auto &expectedOrder = forward ? beforeOrder : afterOrder;
    const auto &targetOrder = forward ? afterOrder : beforeOrder;
    if (!expectedOrder.empty() || !targetOrder.empty())
        require(order(objects) == expectedOrder, "Undo object order conflicts with current state");
    for (const auto &p : patches) {
        const auto &expected = forward ? p.before : p.after;
        const auto &target = forward ? p.after : p.before;
        auto it = std::find_if(objects.begin(), objects.end(),
                               [&](const auto &o) { return o.id == p.id; });
        require(expected ? it != objects.end() && *it == *expected : it == objects.end(),
                "Undo object conflicts with current state");
        if (!target)
            objects.erase(it);
        else if (it == objects.end())
            objects.push_back(*target);
        else
            *it = *target;
    }
    if (!expectedOrder.empty() || !targetOrder.empty()) {
        std::vector<T> sorted;
        sorted.reserve(targetOrder.size());
        for (const auto &id : targetOrder)
            sorted.push_back(std::move(*find(objects, id)));
        objects = std::move(sorted);
    }
}
std::size_t dynamicWeight(const Asset &a) {
    return a.id.str().capacity() + a.relativePath.capacity() + a.sha256.capacity();
}
std::size_t dynamicWeight(const RouteIntent &r) {
    std::size_t n = r.backendId.capacity() + r.portIdentity.capacity() +
                    r.ports.capacity() * sizeof(std::optional<ChannelPortIntent>);
    for (const auto &p : r.ports)
        if (p)
            n += p->deviceIdentity.capacity() + p->portIdentity.capacity() +
                 p->mediaClass.capacity();
    return n;
}
std::size_t dynamicWeight(const std::optional<MasterBus> &m) {
    if (!m)
        return 0;
    std::size_t n = m->id.str().capacity() + dynamicWeight(m->output) +
                    m->plan.tracks.capacity() * sizeof(TrackMix);
    for (const auto &t : m->plan.tracks)
        n += t.track.str().capacity() + t.channels.capacity() * sizeof(ChannelMix);
    return n;
}
std::size_t dynamicWeight(const Track &t) {
    std::size_t n = t.id.str().capacity() + t.name.capacity() + t.eq.id.str().capacity() +
                    t.eq.bands.capacity() * sizeof(EqBand) + t.clips.capacity() * sizeof(Clip);
    for (const auto &b : t.eq.bands)
        n += b.id.str().capacity();
    for (const auto &c : t.clips)
        n += c.id.str().capacity() + c.assetId.str().capacity();
    return n + dynamicWeight(t.input) + dynamicWeight(t.output) + dynamicWeight(t.monitor);
}
} // namespace
Track makeAudioTrack(std::string name, ChannelLayout channels, std::uint32_t rate) {
    Session s;
    s.sampleRate = rate;
    Track t;
    t.name = std::move(name);
    t.layout = channels;
    for (double f : {100., 1000., 10000.})
        if (f < double(rate) / 2) {
            EqBand b;
            b.frequencyHz = f;
            t.eq.bands.push_back(std::move(b));
        }
    s.tracks.push_back(std::move(t));
    validate(s);
    return std::move(s.tracks.front());
}
void applySessionEdits(Session &s, const std::vector<SessionEdit> &edits) {
    require(!edits.empty() && edits.size() <= 64, "Edit batch must contain 1 to 64 operations");
    validate(s);
    auto proposed = s;
    for (const auto &edit : edits) {
        std::visit(
            [&](const auto &e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<E, InsertTrack>) {
                    proposed.tracks.insert(position(proposed.tracks, e.before), e.track);
                } else if constexpr (std::is_same_v<E, RemoveTrack>) {
                    proposed.tracks.erase(find(proposed.tracks, e.track));
                    if (proposed.master)
                        std::erase_if(proposed.master->plan.tracks,
                                      [&](const auto &t) { return t.track == e.track; });
                } else if constexpr (std::is_same_v<E, SetMaster>) {
                    proposed.master = e.value;
                } else if constexpr (std::is_same_v<E, SetPunch>) {
                    proposed.punch = e.value;
                } else if constexpr (std::is_same_v<E, RenameTrack>) {
                    track(proposed, e.track).name = e.name;
                } else if constexpr (std::is_same_v<E, MoveTrack>) {
                    (void)find(proposed.tracks, e.track);
                    if (e.before)
                        (void)find(proposed.tracks, *e.before);
                    if (e.before == std::optional<Id>(e.track))
                        return;
                    auto it = find(proposed.tracks, e.track);
                    auto moved = std::move(*it);
                    proposed.tracks.erase(it);
                    proposed.tracks.insert(position(proposed.tracks, e.before), std::move(moved));
                } else if constexpr (std::is_same_v<E, InsertClip>) {
                    auto &clips = track(proposed, e.track).clips;
                    clips.insert(position(clips, e.before), e.clip);
                } else if constexpr (std::is_same_v<E, RemoveClip>) {
                    auto &clips = track(proposed, e.track).clips;
                    clips.erase(find(clips, e.clip));
                } else if constexpr (std::is_same_v<E, SetClipRange>) {
                    auto &c = *find(track(proposed, e.track).clips, e.clip);
                    c.startFrame = e.start;
                    c.sourceFrame = e.source;
                    c.lengthFrames = e.length;
                } else if constexpr (std::is_same_v<E, MoveClip>) {
                    auto &from = track(proposed, e.from).clips;
                    auto &to = track(proposed, e.to).clips;
                    if (e.before)
                        (void)find(to, *e.before);
                    if (e.from == e.to && e.before == std::optional<Id>(e.clip)) {
                        find(from, e.clip)->startFrame = e.start;
                        return;
                    }
                    auto it = find(from, e.clip);
                    auto moved = std::move(*it);
                    from.erase(it);
                    moved.startFrame = e.start;
                    to.insert(position(to, e.before), std::move(moved));
                } else if constexpr (std::is_same_v<E, SplitClip>) {
                    auto &clips = track(proposed, e.track).clips;
                    auto it = find(clips, e.clip);
                    require(e.position > it->startFrame &&
                                e.position < it->startFrame + it->lengthFrames,
                            "Split must be strictly inside the clip");
                    auto right = *it;
                    const auto leftLength = e.position - it->startFrame;
                    right.id = e.rightId;
                    right.startFrame = e.position;
                    right.sourceFrame += leftLength;
                    right.lengthFrames -= leftLength;
                    it->lengthFrames = leftLength;
                    clips.insert(it + 1, std::move(right));
                }
            },
            edit);
        validate(proposed);
    }
    s = std::move(proposed);
}
std::size_t EditHistory::weight(const Change &change) {
    return sizeof(Change) +
           std::visit(
               [](const auto &c) -> std::size_t {
                   using C = std::decay_t<decltype(c)>;
                   if constexpr (std::is_same_v<C, StructureChange>) {
                       std::size_t n = c.tracks.capacity() * sizeof(ObjectChange<Track>) +
                                       c.assets.capacity() * sizeof(ObjectChange<Asset>);
                       for (const auto &p : c.tracks) {
                           n += p.id.str().capacity();
                           if (p.before)
                               n += dynamicWeight(*p.before);
                           if (p.after)
                               n += dynamicWeight(*p.after);
                       }
                       for (const auto &p : c.assets) {
                           n += p.id.str().capacity();
                           if (p.before)
                               n += dynamicWeight(*p.before);
                           if (p.after)
                               n += dynamicWeight(*p.after);
                       }
                       for (const auto *ids : {&c.trackOrderBefore, &c.trackOrderAfter,
                                               &c.assetOrderBefore, &c.assetOrderAfter}) {
                           n += ids->capacity() * sizeof(Id);
                           for (const auto &id : *ids)
                               n += id.str().capacity();
                       }
                       if (c.master)
                           n += dynamicWeight(c.master->first) + dynamicWeight(c.master->second);
                       return n;
                   } else if constexpr (std::is_same_v<C, RouteChange>) {
                       return c.address.trackId.str().capacity() + dynamicWeight(c.before) +
                              dynamicWeight(c.after);
                   } else
                       return 256; // Conservative fixed identity payload allowance.
               },
               change);
}
void EditHistory::retain(Change change) {
    constexpr std::size_t budget = 32 * 1024 * 1024;
    require(weight(change) <= budget, "Edit exceeds retained history byte budget");
    undo_.push_back(std::move(change)); // Allocation must succeed before model mutation.
    redo_.clear();
    std::size_t total = 0;
    for (const auto &c : undo_)
        total += weight(c);
    while (undo_.size() > 256 || total > budget) {
        total -= weight(undo_.front());
        undo_.erase(undo_.begin());
    }
}
bool EditHistory::adopt(const Session &value) {
    require(!active_, "Cannot adopt structural state during a parameter gesture");
    validate(value);
    require(value.id == session_.id && value.name == session_.name &&
                value.sampleRate == session_.sampleRate &&
                value.playheadFrame == session_.playheadFrame &&
                value.exportStartFrame == session_.exportStartFrame,
            "Structural admission cannot replace project identity or timing preferences");
    StructureChange change;
    differences(session_.tracks, value.tracks, change.tracks);
    differences(session_.assets, value.assets, change.assets);
    const auto oldTracks = order(session_.tracks), newTracks = order(value.tracks);
    if (oldTracks != newTracks) {
        change.trackOrderBefore = oldTracks;
        change.trackOrderAfter = newTracks;
    }
    const auto oldAssets = order(session_.assets), newAssets = order(value.assets);
    if (oldAssets != newAssets) {
        change.assetOrderBefore = oldAssets;
        change.assetOrderAfter = newAssets;
    }
    if (value.exportEndFrame != session_.exportEndFrame)
        change.exportEnd = {{session_.exportEndFrame, value.exportEndFrame}};
    if (value.master != session_.master)
        change.master = {{session_.master, value.master}};
    if (value.punch != session_.punch)
        change.punch = {{session_.punch, value.punch}};
    if (change.tracks.empty() && change.assets.empty() && oldTracks == newTracks &&
        oldAssets == newAssets && !change.exportEnd && !change.master && !change.punch)
        return false;
    auto proposed = value;
    retain(std::move(change));
    session_ = std::move(proposed);
    return true;
}
bool EditHistory::structural(const std::vector<SessionEdit> &edits) {
    require(!active_, "Cannot change structure during a parameter gesture");
    auto proposed = session_;
    applySessionEdits(proposed, edits);
    return adopt(proposed);
}
void EditHistory::apply(const Change &change, bool forward) {
    std::visit(
        [&](const auto &c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, ParameterChange>)
                setParameterValue(session_, c.address, forward ? c.after : c.before);
            else if constexpr (std::is_same_v<C, RouteChange>)
                setRouteValue(session_, c.address, forward ? c.after : c.before);
            else if constexpr (std::is_same_v<C, MonitoringChange>)
                setMonitoringValue(session_, c.trackId, forward ? c.after : c.before);
            else {
                auto proposed = session_;
                overlay(proposed.tracks, c.tracks, c.trackOrderBefore, c.trackOrderAfter, forward);
                overlay(proposed.assets, c.assets, c.assetOrderBefore, c.assetOrderAfter, forward);
                if (c.exportEnd) {
                    require(proposed.exportEndFrame ==
                                (forward ? c.exportEnd->first : c.exportEnd->second),
                            "Undo export extent conflicts with current state");
                    proposed.exportEndFrame = forward ? c.exportEnd->second : c.exportEnd->first;
                }
                if (c.master) {
                    require(proposed.master == (forward ? c.master->first : c.master->second),
                            "Undo master conflicts with current state");
                    proposed.master = forward ? c.master->second : c.master->first;
                }
                if (c.punch) {
                    require(proposed.punch == (forward ? c.punch->first : c.punch->second),
                            "Undo punch locators conflict with current state");
                    proposed.punch = forward ? c.punch->second : c.punch->first;
                }
                validate(proposed);
                session_ = std::move(proposed);
            }
        },
        change);
}
} // namespace soundcurrent::daw
