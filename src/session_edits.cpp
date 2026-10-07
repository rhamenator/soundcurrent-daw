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
    std::unordered_map<std::string_view, const T *> beforeIndex, afterIndex;
    beforeIndex.reserve(before.size());
    afterIndex.reserve(after.size());
    for (const auto &old : before)
        beforeIndex.emplace(old.id.str(), &old);
    for (const auto &now : after)
        afterIndex.emplace(now.id.str(), &now);
    for (const auto &old : before) {
        const auto now = afterIndex.find(old.id.str());
        if (now == afterIndex.end())
            patches.push_back({old.id, old, std::nullopt});
        else if (*now->second != old)
            patches.push_back({old.id, old, *now->second});
    }
    for (const auto &now : after)
        if (!beforeIndex.contains(now.id.str()))
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
        std::unordered_map<std::string, std::size_t> index;
        index.reserve(objects.size());
        for (std::size_t n = 0; n < objects.size(); ++n)
            index.emplace(objects[n].id.str(), n);
        std::vector<T> sorted;
        sorted.reserve(targetOrder.size());
        for (const auto &id : targetOrder) {
            const auto found = index.find(id.str());
            require(found != index.end(), "Undo object no longer exists", ErrorCode::InvalidId);
            sorted.push_back(std::move(objects[found->second]));
        }
        objects = std::move(sorted);
    }
}
void stringWeight(PayloadCharge &charge, const std::string &s) {
    charge.add(s.capacity());
    charge.add(1);
}
void dynamicWeight(PayloadCharge &charge, const Asset &a) {
    stringWeight(charge, a.id.str());
    stringWeight(charge, a.relativePath);
    stringWeight(charge, a.sha256);
}
void dynamicWeight(PayloadCharge &charge, const RouteIntent &r) {
    stringWeight(charge, r.backendId);
    stringWeight(charge, r.portIdentity);
    charge.add(r.ports.capacity(), sizeof(std::optional<ChannelPortIntent>));
    for (const auto &p : r.ports)
        if (p) {
            stringWeight(charge, p->deviceIdentity);
            stringWeight(charge, p->portIdentity);
            stringWeight(charge, p->mediaClass);
        }
}
void dynamicWeight(PayloadCharge &charge, const std::optional<MasterBus> &m) {
    if (!m)
        return;
    stringWeight(charge, m->id.str());
    dynamicWeight(charge, m->output);
    charge.add(m->plan.tracks.capacity(), sizeof(TrackMix));
    for (const auto &t : m->plan.tracks) {
        stringWeight(charge, t.track.str());
        charge.add(t.channels.capacity(), sizeof(ChannelMix));
    }
}
void dynamicWeight(PayloadCharge &charge, const Track &t) {
    stringWeight(charge, t.id.str());
    stringWeight(charge, t.name);
    stringWeight(charge, t.eq.id.str());
    charge.add(t.eq.bands.capacity(), sizeof(EqBand));
    charge.add(t.clips.capacity(), sizeof(Clip));
    for (const auto &b : t.eq.bands)
        stringWeight(charge, b.id.str());
    for (const auto &c : t.clips) {
        stringWeight(charge, c.id.str());
        stringWeight(charge, c.assetId.str());
    }
    dynamicWeight(charge, t.input);
    dynamicWeight(charge, t.output);
    dynamicWeight(charge, t.monitor);
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
void applySessionEdits(Session &s, const std::vector<SessionEdit> &edits, StateBudget budget) {
    require(!edits.empty() && edits.size() <= 64, "Edit batch must contain 1 to 64 operations");
    validate(s, budget);
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
                } else if constexpr (std::is_same_v<E, SetInputLatency>) {
                    track(proposed, e.track).inputLatencyFrames = e.frames;
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
        validate(proposed, budget);
    }
    s = std::move(proposed);
}
void validateHistoryBudget(const HistoryBudget &budget) {
    require(budget.retainedBytes && budget.operationBytes && budget.maximumCommands,
            "Undo limits must be positive", ErrorCode::InvalidParameter);
}
EditHistory::EditHistory(Session &session, StateBudget budget, HistoryBudget history)
    : session_(session), budget_(budget), historyBudget_(history) {
    validateHistoryBudget(history);
}
std::size_t EditHistory::weight(const Change &change) {
    PayloadCharge charge("Undo payload", std::numeric_limits<std::size_t>::max());
    charge.add(sizeof(Entry));
    // Declared storage allowance per deque entry; this is not allocator/RSS accounting.
    charge.add(128);
    std::visit(
        [&](const auto &c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, StructureChange>) {
                charge.add(c.tracks.capacity(), sizeof(ObjectChange<Track>));
                charge.add(c.assets.capacity(), sizeof(ObjectChange<Asset>));
                for (const auto &p : c.tracks) {
                    stringWeight(charge, p.id.str());
                    if (p.before)
                        dynamicWeight(charge, *p.before);
                    if (p.after)
                        dynamicWeight(charge, *p.after);
                }
                for (const auto &p : c.assets) {
                    stringWeight(charge, p.id.str());
                    if (p.before)
                        dynamicWeight(charge, *p.before);
                    if (p.after)
                        dynamicWeight(charge, *p.after);
                }
                for (const auto *ids : {&c.trackOrderBefore, &c.trackOrderAfter,
                                        &c.assetOrderBefore, &c.assetOrderAfter}) {
                    charge.add(ids->capacity(), sizeof(Id));
                    for (const auto &id : *ids)
                        stringWeight(charge, id.str());
                }
                if (c.master) {
                    dynamicWeight(charge, c.master->first);
                    dynamicWeight(charge, c.master->second);
                }
            } else if constexpr (std::is_same_v<C, RouteChange>) {
                stringWeight(charge, c.address.trackId.str());
                dynamicWeight(charge, c.before);
                dynamicWeight(charge, c.after);
            } else if constexpr (std::is_same_v<C, ParameterChange>) {
                stringWeight(charge, c.address.trackId.str());
                stringWeight(charge, c.address.processorId.str());
                stringWeight(charge, c.address.bandId.str());
            } else
                stringWeight(charge, c.trackId.str());
        },
        change);
    return charge.bytes();
}
HistoryResources EditHistory::resources() const {
    return {undo_.size(),        redo_.size(),    retainedBytes_, active_ ? weight(*active_) : 0,
            operationPeakBytes_, evictedCommands_};
}
void EditHistory::configure(HistoryBudget budget) {
    validateHistoryBudget(budget);
    require(!active_, "Finish or cancel the active gesture before changing Undo limits");
    if (retainedBytes_ > budget.retainedBytes)
        throw ResourceLimitError("Retained Undo", retainedBytes_, budget.retainedBytes);
    if (undo_.size() + redo_.size() > budget.maximumCommands)
        throw ProjectError(ErrorCode::ResourceLimit, "Undo command limit is below retained usage");
    PayloadCharge charge("Undo operation", budget.operationBytes);
    charge.add(retainedBytes_);
    charge.add(sessionPayloadBytes(session_, budget_));
    historyBudget_ = budget;
}
std::size_t EditHistory::checkOperation(std::size_t candidateBytes, const Session &value,
                                        bool pending, std::size_t extraBytes) const {
    PayloadCharge charge("Undo operation", historyBudget_.operationBytes);
    charge.add(retainedBytes_);
    charge.add(extraBytes);
    if (pending && active_)
        charge.add(weight(*active_));
    // Candidate and staging copy, plus canonical/candidate/validation-overlay work.
    charge.add(candidateBytes, 2);
    charge.add(
        std::max(sessionPayloadBytes(session_, budget_), sessionPayloadBytes(value, budget_)), 3);
    return charge.bytes();
}
std::size_t EditHistory::checkCandidate(const Change &change, const Session &value) const {
    const auto bytes = weight(change);
    if (bytes > historyBudget_.retainedBytes)
        throw ResourceLimitError("Retained Undo command", bytes, historyBudget_.retainedBytes);
    if (active_ && weight(*active_) > historyBudget_.retainedBytes)
        throw ResourceLimitError("Pending Undo command", weight(*active_),
                                 historyBudget_.retainedBytes);
    return checkOperation(bytes, value);
}
std::size_t EditHistory::checkBegin(const ParameterAddress &address) const {
    const auto value = parameterValue(session_, address);
    const Change candidate = ParameterChange{address, value, value};
    checkCandidate(candidate, session_);
    return checkOperation(weight(candidate), session_, true, weight(candidate));
}
std::size_t EditHistory::checkRoute(const RouteAddress &address, const RouteIntent &value) const {
    auto next = session_;
    setRouteValue(next, address, value, budget_);
    return checkCandidate(RouteChange{address, routeValue(session_, address), value}, next);
}
std::size_t EditHistory::checkMonitoring(const Id &id, RecordingMonitor value) const {
    auto next = session_;
    setMonitoringValue(next, id, value);
    return checkCandidate(MonitoringChange{id, monitoringValue(session_, id), value}, next);
}
std::size_t EditHistory::checkAdopt(const Session &value) const {
    if (value == session_)
        return 0;
    return checkCandidate(difference(value), value);
}
void EditHistory::acceptPreflight(std::size_t declaredBytes) noexcept {
    operationPeakBytes_ = std::max(operationPeakBytes_, declaredBytes);
}
void EditHistory::retain(Change change, const Session &value) {
    checkCandidate(change, value);
    const auto bytes = weight(change);
    const auto peak = checkOperation(bytes, value);
    std::size_t total = bytes;
    // Compute eviction before mutation; no throwing accounting after the insertion.
    for (const auto &entry : undo_) {
        PayloadCharge sum("Retained Undo", std::numeric_limits<std::size_t>::max());
        sum.add(total);
        sum.add(entry.bytes);
        total = sum.bytes();
    }
    std::size_t drops = 0;
    for (const auto &entry : undo_) {
        if (undo_.size() + 1 - drops <= historyBudget_.maximumCommands &&
            total <= historyBudget_.retainedBytes)
            break;
        total -= entry.bytes;
        ++drops;
    }
    undo_.push_back({std::move(change), bytes}); // Strong guarantee on allocation failure.
    redo_.clear();
    for (std::size_t n = 0; n < drops; ++n)
        undo_.pop_front();
    retainedBytes_ = total;
    operationPeakBytes_ = std::max(operationPeakBytes_, peak);
    evictedCommands_ = drops > std::numeric_limits<std::uint64_t>::max() - evictedCommands_
                           ? std::numeric_limits<std::uint64_t>::max()
                           : evictedCommands_ + drops;
}
EditHistory::StructureChange EditHistory::difference(const Session &value) const {
    validate(value, budget_);
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
    return change;
}
bool EditHistory::adopt(const Session &value) {
    require(!active_, "Cannot adopt structural state during a parameter gesture");
    auto change = difference(value);
    if (value == session_)
        return false;
    checkCandidate(change, value);
    auto next = value;
    retain(std::move(change), next);
    session_ = std::move(next);
    return true;
}
bool EditHistory::structural(const std::vector<SessionEdit> &edits) {
    require(!active_, "Cannot change structure during a parameter gesture");
    auto proposed = session_;
    applySessionEdits(proposed, edits, budget_);
    return adopt(proposed);
}
Session EditHistory::proposed(const Change &change, bool forward) const {
    auto next = session_;
    std::visit(
        [&](const auto &c) {
            using C = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<C, ParameterChange>)
                setParameterValue(next, c.address, forward ? c.after : c.before);
            else if constexpr (std::is_same_v<C, RouteChange>)
                setRouteValue(next, c.address, forward ? c.after : c.before, budget_);
            else if constexpr (std::is_same_v<C, MonitoringChange>)
                setMonitoringValue(next, c.trackId, forward ? c.after : c.before);
            else {
                auto &proposed = next;
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
                validate(proposed, budget_);
            }
        },
        change);
    validate(next, budget_);
    return next;
}
} // namespace soundcurrent::daw
