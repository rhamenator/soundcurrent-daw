// SPDX-License-Identifier: GPL-3.0-only
#include "writer_timing.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
namespace {
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
nlohmann::json report(const native_fixture::WriterTiming &t) {
    std::ostringstream s;
    t.write(s);
    return nlohmann::json::parse(s.str());
}
RecordingWriterObservation event(RecordingWriterPhase phase, Frame written, Frame committed,
                                 unsigned ready) {
    return {phase, written, committed, {ready, 0, ready * 4096ULL, 131072}, true};
}
} // namespace
int main() {
    try {
        native_fixture::WriterTiming t;
        t.record({RecordingWriterPhase::JournalPublishBegin}, 1); // No attached producer.
        t.record(event(RecordingWriterPhase::WriteHashBegin, 0, 0, 2), 10);
        t.record(event(RecordingWriterPhase::WriteHashEnd, 4096, 0, 3), 20);
        t.record(event(RecordingWriterPhase::AudioFlushBegin, 4096, 0, 3), 21);
        t.record(event(RecordingWriterPhase::AudioFlushEnd, 4096, 0, 4), 32);
        t.record(event(RecordingWriterPhase::JournalPublishBegin, 4096, 0, 4), 33);
        t.record(event(RecordingWriterPhase::JournalPublishEnd, 4096, 4096, 32), 4033);
        t.record(event(RecordingWriterPhase::IdleBegin, 4096, 4096, 0), 4034);
        t.record(event(RecordingWriterPhase::IdleEnd, 4096, 4096, 1), 4040);
        t.record(event(RecordingWriterPhase::WriteHashBegin, 4096, 4096, 1), 4050);
        t.record(event(RecordingWriterPhase::WriteHashEnd, 8192, 4096, 0), 4059);
        auto d = report(t);
        require(d["phase_pairs_complete"] && d["maximum_ready_slabs"] == 32 &&
                    d["maximum_queued_frame_upper_bound"] == 131072 &&
                    d["maximum_write_gap_ns"] == 4030,
                "Bounded occupancy/write gap report differs");
        const auto p = d["phase_summaries"];
        require(p[0]["count"] == 2 && p[0]["maximum_ns"] == 10 && p[1]["maximum_ns"] == 11 &&
                    p[2]["maximum_ns"] == 4000 && p[2]["end_ready_slabs"] == 32 &&
                    p[2]["begin_committed_frames"] == 0 && p[2]["end_committed_frames"] == 4096 &&
                    p[3]["maximum_ns"] == 6,
                "Phase pairing or maximum context differs");
        t.record(event(RecordingWriterPhase::IdleBegin, 8192, 4096, 0), 5000);
        require(!report(t)["phase_pairs_complete"].get<bool>(), "Unfinished phase was hidden");
        native_fixture::WriterTiming missing;
        missing.record(event(RecordingWriterPhase::AudioFlushEnd, 0, 0, 0), 4);
        missing.record(event(RecordingWriterPhase::IdleBegin, 0, 0, 0), 8);
        missing.record(event(RecordingWriterPhase::IdleBegin, 0, 0, 0), 9);
        missing.record(event(RecordingWriterPhase::IdleEnd, 0, 0, 0), 7);
        require(report(missing)["missing_phase_pairs"] == 3,
                "Missing/repeated/backwards phase errors were hidden");
        std::cout << "Writer phase pairing, fixed maxima/context, backlog, gap, construction "
                     "exclusion and missing observations passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
