// SPDX-License-Identifier: GPL-3.0-only
#include "waveform_cache.hpp"
#include <soundcurrent/waveform_projection.hpp>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void wait(F f) { QElapsedTimer t; t.start(); while (!f()) { if (t.elapsed() > 10000) throw std::runtime_error("Waveform cache observation deadline"); QTest::qWait(2); } }
template<class F> void refused(F f, ErrorCode code) { bool caught = false; try { f(); } catch (const ProjectError &e) { caught = true; check(e.code() == code, "Wrong cache refusal"); } check(caught, "Missing cache refusal"); }
void put(std::string &s, std::uint64_t n, unsigned bytes) { for (unsigned i = 0; i < bytes; ++i) s += char((n >> (8 * i)) & 255); }
WaveformKey fixture(const std::filesystem::path &root, std::string name, double value) {
    std::string s = "RIFF"; put(s, 36 + 80, 4); s += "WAVEfmt "; put(s, 16, 4); put(s, 3, 2); put(s, 1, 2);
    put(s, 48000, 4); put(s, 384000, 4); put(s, 8, 2); put(s, 64, 2); s += "data"; put(s, 80, 4);
    for (unsigned f = 0; f < 10; ++f) put(s, std::bit_cast<std::uint64_t>(value + f), 8);
    { std::ofstream out(root / utf8Path(name), std::ios::binary); out << s; check(bool(out), "Cache source fixture failed"); }
    ResourceLedger scratch; ApprovedMediaRoot approved(root, scratch); auto file = approved.open(name, 1024);
    return {{file.digest(), 10, 48000, 1}, std::move(name), 2};
}
auto ready(WaveformCache &cache, const std::filesystem::path &root, std::uint64_t epoch, const WaveformKey &key) {
    wait([&] { cache.poll(); auto v = cache.lookup(root, epoch, key); return v.phase == CachedWaveformPhase::Ready || v.phase == CachedWaveformPhase::Fault; });
    auto result = cache.lookup(root, epoch, key); check(result.phase == CachedWaveformPhase::Ready && result.waveform, "Expected cached waveform missing"); return result.waveform;
}
struct Release { std::atomic<bool> &flag; ~Release() { flag = true; } };
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto root = std::filesystem::temp_directory_path() / ("sc-waveform-cache-" + Id::generate().str());
    try {
        check(std::filesystem::create_directory(root), "No cache fixture folder");
        const auto a = fixture(root, "a-é.wav", -3), b = fixture(root, "b-日本.wav", 12), c = fixture(root, "c.wav", 30);
        ResourceLedger memory(512 * 1024 * 1024); std::atomic<unsigned> launches = 0;
        const auto options = [&] { WaveformCacheOptions o; o.maximumEntries = 2; o.controller.memory = memory; o.controller.beforeSpawn = [&] { ++launches; }; return o; };
        std::shared_ptr<const Waveform> borrowed;
        {
            WaveformCache cache(options()); cache.setVisible(root, 1, {a, b});
            check(!cache.workerSnapshot()->childPid && cache.lookup(root, 1, a).phase == CachedWaveformPhase::Pending, "Admission launched work or invented a result");
            borrowed = ready(cache, root, 1, a); auto second = ready(cache, root, 1, b);
            check(borrowed->peak(0, 0, 0) == WaveformPeak{-3, -2} && second->peak(0, 0, 0) == WaveformPeak{12, 13}, "Different source peaks collided");
            check(cache.residentBytes() == borrowed->chargedBytes() + second->chargedBytes(), "Resident cache accounting differs");
            for (const auto rate : {8000U, 44100U, 48000U, 96000U, 384000U})
                for (const auto speed : {ClipPlaybackRate{1, 4}, ClipPlaybackRate{1, 1}, ClipPlaybackRate{3, 2}, ClipPlaybackRate{4, 1}}) {
                    Clip clip; clip.startFrame = 1000000000000000LL; clip.sourceFrame = 2;
                    clip.sourceTiming = {1, 3}; clip.playbackRate = speed; clip.lengthFrames = 12;
                    for (Frame first = 0; first <= 12; ++first) for (Frame end = first; end <= 12; ++end)
                        for (std::size_t level = 0; level < borrowed->levels().size(); ++level) {
                            const auto span = waveformCoverage(*borrowed, clip, rate, level, first, end);
                            // Independent small-integer rational oracle: test each
                            // source sample's membership without SourceFrameMap.
                            const std::uint64_t d = std::uint64_t(rate) * speed.denominator * 3;
                            const auto left = 7 * std::uint64_t(rate) * speed.denominator + std::uint64_t(first) * 48000 * speed.numerator * 3;
                            const auto right = 7 * std::uint64_t(rate) * speed.denominator + std::uint64_t(end) * 48000 * speed.numerator * 3;
                            for (std::uint64_t sample = 0; sample < 10; ++sample)
                                check((sample >= span.sourceBegin && sample < span.sourceEnd) == (sample * d >= left && sample * d < right), "Source membership differs from rational oracle");
                            for (std::uint64_t bin = 0; bin < borrowed->levels()[level].bins; ++bin) {
                                const auto [lo, hi] = borrowed->sourceRange(level, bin);
                                check((bin >= span.firstFullBin && bin < span.endFullBin) == (span.sourceBegin < span.sourceEnd && lo >= span.sourceBegin && hi <= span.sourceEnd), "Partial source bin exposed as exact viewport data");
                                const auto projected = waveformBinProjectRange(*borrowed, clip, rate, level, bin);
                                const auto expected = [&](std::uint64_t at) { return (static_cast<long double>(at) * 3 - 7) * rate * speed.denominator / (3 * 48000.L * speed.numerator); };
                                check(std::abs(projected.first - expected(lo)) < 1e-10L && std::abs(projected.second - expected(hi)) < 1e-10L, "Bin-to-project geometry differs");
                            }
                        }
                }
            const auto count = launches.load(); cache.setVisible(root, 1, {a, b}); cache.poll();
            check(launches == count, "Unchanged viewport rebuilt audio");
            refused([&] { cache.setVisible(root, 1, {a, b, c}); }, ErrorCode::ResourceLimit);
            refused([&] { cache.setVisible(root, 1, {a, a}); }, ErrorCode::InvalidParameter);
            check(cache.lookup(root, 1, a).waveform == borrowed, "Failed visible admission changed prior cache");
            cache.setVisible(root, 1, {a}); cache.poll();
            check(cache.lookup(root, 1, b).phase == CachedWaveformPhase::Missing, "Offscreen data exposed as current");
            cache.setVisible(root, 1, {a, b}); cache.poll();
            check(cache.lookup(root, 1, b).waveform == second && launches == count, "Offscreen reuse unnecessarily decoded source");
            second.reset(); cache.setVisible(root, 1, {a, c}); ready(cache, root, 1, c);
            check(cache.entryCount() == 2 && cache.lookup(root, 1, a).waveform == borrowed, "Wanted entry evicted for another wanted entry");
            cache.setVisible(root, 2, {a});
            check(cache.lookup(root, 1, a).phase == CachedWaveformPhase::Missing && !cache.lookup(root, 2, a).waveform, "Previous project opening leaked into current cache");
            auto replacement = ready(cache, root, 2, a); check(replacement != borrowed, "New opening reused old authority");
            check(borrowed->peak(0, 0, 0).minimum == -3, "Eviction invalidated borrower storage");
            cache.requestShutdown(); wait([&] { return cache.closed(); });
            check(!cache.entryCount() && cache.lookup(root, 2, a).phase == CachedWaveformPhase::Missing, "Shutdown exposed cache entries");
        }
        check(memory.usage().reservedBytes == borrowed->chargedBytes(), "Cache metadata/controller credit leaked"); borrowed.reset();
        check(memory.usage().reservedBytes == 0, "Borrowed cache credit not retired");
        {
            WaveformCache cache(options()); auto left=a, right=a;
            left.window={1,5}; right.window={4,5};
            cache.setVisible(root,1,{left,right});
            auto l=ready(cache,root,1,left), r=ready(cache,root,1,right);
            check(l!=r && l->peak(0,0,0)==WaveformPeak{-2,-1} && r->peak(0,0,0)==WaveformPeak{1,2},"Different source windows collided");
            const auto spawned=launches.load();cache.setVisible(root,1,{left,right});cache.poll();
            check(launches==spawned,"Identical windows rebuilt");
            for (const auto &wave : {l,r}) for (Frame first=0;first<=10;++first) for(Frame end=first;end<=10;++end) {
                Clip clip;clip.lengthFrames=10;
                for(std::size_t level=0;level<wave->levels().size();++level) {
                    const auto span=waveformCoverage(*wave,clip,48000,level,first,end);
                    for(std::uint64_t bin=0;bin<wave->levels()[level].bins;++bin) {
                        const auto [begin,stop]=wave->sourceRange(level,bin);
                        check((bin>=span.firstFullBin && bin<span.endFullBin)==(first<end && begin>=std::uint64_t(first) && stop<=std::uint64_t(end)),"Window crop includes neighboring extrema");
                        check(waveformBinProjectRange(*wave,clip,48000,level,bin)==std::pair<long double,long double>{begin,stop},"Window projection lost absolute source offset");
                    }
                }
            }
            auto invalid=left;invalid.window={9,2};
            refused([&]{cache.setVisible(root,1,{invalid});},ErrorCode::InvalidParameter);
            check(cache.lookup(root,1,left).waveform==l,"Invalid window changed prior cache");
        }
        {
            auto o = options(); o.maximumResidentBytes = 1; WaveformCache cache(o);
            refused([&] { cache.setVisible(root, 1, {a}); }, ErrorCode::ResourceLimit);
            check(!cache.entryCount() && !cache.workerSnapshot()->childPid, "Over-budget visible set partially prepared");
        }
        {
            auto o = options(); WaveformCache cache(o); auto missing = a; missing.relative = "later.wav";
            cache.setVisible(root, 1, {missing});
            wait([&] { cache.poll(); return cache.lookup(root, 1, missing).phase == CachedWaveformPhase::Fault; });
            check(cache.lookup(root, 1, missing).error == ErrorCode::MissingMedia, "Cache lost media error");
            const auto failed = launches.load(); cache.setVisible(root, 1, {missing}); for (unsigned i = 0; i < 10; ++i) cache.poll();
            check(launches == failed, "Faulted visible key retried without user intent");
            std::filesystem::copy_file(root / utf8Path(a.relative), root / "later.wav"); cache.retryFaults(); ready(cache, root, 1, missing);
        }
        {
            std::atomic<bool> entered = false, release = false; auto o = options();
            o.controller.afterChild = [&](auto, auto) { entered = true; while (!release) QThread::msleep(1); };
            WaveformCache cache(o); Release unlock{release}; cache.setVisible(root, 1, {a}); cache.poll();
            wait([&] { return entered.load(); });
            cache.setVisible(root, 2, {b}); release = true; auto replacement = ready(cache, root, 2, b);
            check(replacement->source() == b.source && cache.lookup(root, 1, a).phase == CachedWaveformPhase::Missing, "Late old-owner result published into replacement");
        }
        {
            WaveformCache cache(options()); cache.setVisible(root, 1, {a}); auto old = ready(cache, root, 1, a);
            const auto changed = fixture(root, a.relative, 99);
            cache.invalidateSources(); check(!cache.lookup(root, 1, a).waveform, "Source invalidation retained displayed peaks");
            wait([&] { cache.poll(); return cache.lookup(root, 1, a).phase == CachedWaveformPhase::Fault; });
            check(cache.lookup(root, 1, a).error == ErrorCode::MediaMismatch, "Changed bytes accepted under old digest");
            cache.setVisible(root, 1, {changed}); auto fresh = ready(cache, root, 1, changed);
            check(fresh->peak(0, 0, 0).minimum == 99 && old->peak(0, 0, 0).minimum == -3, "Source change mutated or confused cache borrowers");
        }
        check(memory.usage().reservedBytes == 0 && memory.usage().owners == 0, "Final cache ownership leaked");
        std::filesystem::remove_all(root); std::cout << checks << " waveform cache checks passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << "\nRetained owned cache fixture: " << root.string() << '\n'; return 1; }
}
