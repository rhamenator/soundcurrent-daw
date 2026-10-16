// SPDX-License-Identifier: GPL-3.0-only
#include "waveform_cache.hpp"
#include <algorithm>
#include <mutex>

namespace soundcurrent::daw::ui {
namespace {
void require(bool good, const char *message, ErrorCode code = ErrorCode::InvalidParameter) {
    if (!good) throw ProjectError(code, message);
}
WaveformTicket ticket(const WaveformKey &key, std::uint64_t epoch, std::uint64_t generation, Id id) {
    return {std::move(id), epoch, generation, key.source, key.baseFramesPerBin, key.window};
}
std::size_t charge(const WaveformKey &key) {
    WaveformLimits limits; limits.baseFramesPerBin = key.baseFramesPerBin; limits.window = key.window;
    // Conservative retained-model allowance; the wire adds its header whereas
    // the model adds fixed metadata. Never derive memory from child-provided sizes.
    return waveformReplySize(ticket(key, 1, 1, Id("00000000-0000-0000-0000-000000000001")), limits) + sizeof(Waveform);
}
struct CurrentSelection {
    std::mutex mutex;
    std::optional<WaveformSelection> value;
    auto get() { std::lock_guard lock(mutex); return value; }
    void set(std::optional<WaveformSelection> next) { std::lock_guard lock(mutex); value = std::move(next); }
};
} // namespace
struct WaveformCache::State {
    struct Entry { WaveformKey key; CachedWaveform value; std::uint64_t age = 0; };
    WaveformCacheOptions options;
    ResourceLease metadata;
    std::shared_ptr<CurrentSelection> current = std::make_shared<CurrentSelection>();
    std::unique_ptr<WaveformController> worker;
    std::filesystem::path root;
    std::uint64_t epoch = 0, generation = 0, age = 0;
    std::vector<Entry> entries;
    std::vector<WaveformKey> visible;
    std::optional<WaveformSelection> active;
    bool closing = false;
    explicit State(WaveformCacheOptions o) : options(std::move(o)) {
        require(options.maximumEntries && options.maximumEntries <= 4096 && options.maximumResidentBytes,
                "Invalid trusted waveform cache policy");
        PayloadCharge allocation("Waveform cache metadata", options.controller.memory.usage().limitBytes);
        allocation.add(65536); allocation.add(options.maximumEntries, 8192);
        metadata = options.controller.memory.reserve(allocation.bytes());
        entries.reserve(options.maximumEntries); visible.reserve(options.maximumEntries);
        options.controller.current = [p = current] { return p->get(); };
        worker = std::make_unique<WaveformController>(options.controller);
    }
    auto find(const WaveformKey &key) { return std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return e.key == key; }); }
    bool wanted(const WaveformKey &key) const { return std::find(visible.begin(), visible.end(), key) != visible.end(); }
    std::size_t resident() const {
        std::size_t bytes = 0;
        for (const auto &e : entries) if (e.value.waveform) bytes += e.value.waveform->chargedBytes();
        return bytes;
    }
    std::uint64_t touch() {
        if (age == UINT64_MAX) { for (auto &e : entries) e.age = 0; age = 0; }
        return ++age;
    }
};
WaveformCache::WaveformCache(WaveformCacheOptions o) : state_(std::make_unique<State>(std::move(o))) {}
WaveformCache::~WaveformCache() { requestShutdown(); }
void WaveformCache::setVisible(std::filesystem::path root, std::uint64_t epoch, std::vector<WaveformKey> keys) {
    auto &s = *state_;
    require(!s.closing, "Waveform cache closing", ErrorCode::InvalidState);
    require(root.is_absolute() && root.native().size() <= 4096 && epoch, "Invalid waveform cache project");
    const auto encoded = root.u8string(); const std::string_view utf8(reinterpret_cast<const char *>(encoded.data()), encoded.size());
    require(utf8.size() <= 4096 && validUtf8(utf8) && utf8.find('\0') == utf8.npos, "Invalid waveform cache root encoding");
    require(keys.size() <= s.options.maximumEntries, "Waveform visible-key count exceeds declared cache", ErrorCode::ResourceLimit);
    PayloadCharge total("Visible waveform models", s.options.maximumResidentBytes);
    // Bound and validate before any cache/context mutation. Caller owns its input
    // vector; retained fixed metadata banks already cover both copies of names.
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto &key = keys[i];
        require(!key.relative.empty() && key.relative.size() <= 1024 && validUtf8(key.relative) &&
            key.relative.find('\0') == key.relative.npos, "Invalid waveform media reference");
        require(std::find(keys.begin(), keys.begin() + std::ptrdiff_t(i), key) == keys.begin() + std::ptrdiff_t(i), "Duplicate visible waveform key");
        total.add(charge(key));
    }
    if (s.root == root && s.epoch == epoch && s.visible == keys) return;
    require(s.generation != UINT64_MAX, "Waveform view generation exhausted", ErrorCode::ResourceLimit);
    // Preconstruct the entire replacement metadata so allocation failure cannot
    // leave a half-admitted view. All memory is covered by the metadata grant.
    std::vector<State::Entry> next; next.reserve(s.options.maximumEntries);
    std::vector<WaveformKey> nextVisible; nextVisible.reserve(s.options.maximumEntries);
    for (const auto &key : keys) nextVisible.push_back(key);
    // Do not retain a caller's arbitrarily oversized vector/string/path capacity.
    std::filesystem::path nextRoot(std::u8string(encoded.data(), encoded.size()));
    const bool sameProject = s.root == root && s.epoch == epoch;
    for (const auto &key : keys) {
        const auto old = sameProject ? s.find(key) : s.entries.end();
        if (old != s.entries.end()) {
            next.push_back(*old);
            if (next.back().value.phase == CachedWaveformPhase::Loading) next.back().value = {CachedWaveformPhase::Pending, {}, {}};
        } else next.push_back({key, {CachedWaveformPhase::Pending, {}, {}}, 0});
    }
    std::size_t planned = total.bytes();
    // Preserve most recently used offscreen entries only with spare entry AND
    // model capacity. Wanted entries are never evicted to load another wanted one.
    if (sameProject) {
        std::vector<const State::Entry *> spare; spare.reserve(s.entries.size());
        for (const auto &e : s.entries) if (std::find(keys.begin(), keys.end(), e.key) == keys.end() && e.value.phase == CachedWaveformPhase::Ready) spare.push_back(&e);
        std::sort(spare.begin(), spare.end(), [](auto a, auto b) { return a->age > b->age; });
        for (const auto *e : spare) {
            const auto bytes = charge(e->key);
            if (next.size() == s.options.maximumEntries) break;
            if (bytes <= s.options.maximumResidentBytes - planned) { next.push_back(*e); planned += bytes; }
        }
    }
    s.current->set(std::nullopt); s.worker->requestCancel();
    s.root = std::move(nextRoot); s.epoch = epoch; ++s.generation;
    s.visible = std::move(nextVisible); s.entries = std::move(next);
}
void WaveformCache::retryFaults() {
    auto &s = *state_; if (s.closing) return;
    for (auto &e : s.entries) if (s.wanted(e.key) && e.value.phase == CachedWaveformPhase::Fault)
        e.value = {CachedWaveformPhase::Pending, {}, {}};
}
void WaveformCache::invalidateSources() {
    auto &s = *state_; if (s.closing) return;
    require(s.generation != UINT64_MAX, "Waveform view generation exhausted", ErrorCode::ResourceLimit);
    s.current->set(std::nullopt); s.worker->requestCancel(); ++s.generation;
    std::erase_if(s.entries, [&](const auto &e) { return !s.wanted(e.key); });
    for (auto &e : s.entries) e.value = {CachedWaveformPhase::Pending, {}, {}};
}
void WaveformCache::poll() {
    auto &s = *state_; auto status = s.worker->snapshot();
    if (s.closing || status->busy) return;
    if (s.active) {
        const auto expected = *s.active;
        const WaveformKey key{expected.ticket.source, expected.relative, expected.ticket.baseFramesPerBin, expected.ticket.window};
        const auto found = s.find(key);
        const auto current = s.current->get();
        if (current && *current == expected && found != s.entries.end() && s.wanted(key)) {
            if (auto wave = status->resultFor(expected)) {
                require(wave->chargedBytes() <= charge(key), "Waveform model exceeds admitted cache slot", ErrorCode::ResourceLimit);
                found->value = {CachedWaveformPhase::Ready, std::move(wave), {}}; found->age = s.touch();
            } else found->value = {CachedWaveformPhase::Fault, {}, status->error.value_or(ErrorCode::Io)};
        }
        s.active.reset(); s.current->set(std::nullopt); s.worker->clearResult();
    }
    status.reset(); // Release obsolete receive/result borrowers before next admission.
    const auto next = std::find_if(s.entries.begin(), s.entries.end(), [&](const auto &e) { return s.wanted(e.key) && e.value.phase == CachedWaveformPhase::Pending; });
    if (next == s.entries.end()) return;
    try {
        WaveformSelection selection{ticket(next->key, s.epoch, s.generation, Id::generate()), s.root, next->key.relative};
        s.current->set(selection);
        if (s.worker->submit(selection) == Admission::Accepted) {
            s.active = std::move(selection); next->value.phase = CachedWaveformPhase::Loading;
        } else { s.current->set(std::nullopt); next->value = {CachedWaveformPhase::Fault, {}, ErrorCode::InvalidState}; }
    } catch (const ProjectError &e) { s.current->set(std::nullopt); next->value = {CachedWaveformPhase::Fault, {}, e.code()}; }
    catch (const std::exception &) { s.current->set(std::nullopt); next->value = {CachedWaveformPhase::Fault, {}, ErrorCode::Io}; }
}
CachedWaveform WaveformCache::lookup(const std::filesystem::path &root, std::uint64_t epoch, const WaveformKey &key) const {
    auto &s = *state_;
    if (s.closing || root != s.root || epoch != s.epoch || !s.wanted(key)) return {};
    const auto found = s.find(key); if (found == s.entries.end()) return {};
    found->age = s.touch(); return found->value;
}
void WaveformCache::requestShutdown() noexcept {
    if (!state_ || state_->closing) return;
    state_->closing = true; state_->current->set(std::nullopt); state_->worker->requestShutdown();
    state_->visible.clear(); state_->entries.clear(); state_->active.reset();
}
bool WaveformCache::closed() const { return state_->worker->snapshot()->closed; }
std::size_t WaveformCache::residentBytes() const noexcept { return state_->resident(); }
std::size_t WaveformCache::entryCount() const noexcept { return state_->entries.size(); }
std::shared_ptr<const WaveformSnapshot> WaveformCache::workerSnapshot() const { return state_->worker->snapshot(); }
} // namespace soundcurrent::daw::ui
