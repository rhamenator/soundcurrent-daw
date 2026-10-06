// SPDX-License-Identifier: GPL-3.0-only
#include "native_duration_timing.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
using native_fixture::DurationTiming;
using soundcurrent::daw::DeviceBlockClock;
void require(bool ok) {
    if (!ok)
        throw std::runtime_error("Duration timing contract failed");
}
nlohmann::json summary(const DurationTiming &t) {
    std::ostringstream o;
    t.write(o);
    return nlohmann::json::parse(o.str());
}
int main() {
    DeviceBlockClock c{};
    c.duration = 1;
    c.rateNumerator = 1;
    c.rateDenominator = 100000;
    DurationTiming t(1001);
    for (unsigned n = 1; n <= 1001; ++n)
        t.record(n, &c);
    auto s = summary(t);
    require(s["p50_ns"] == 501 && s["p999_ns"] == 1000 && s["maximum_ns"] == 1001 &&
            s["complete_timing_coverage"] == true && s["finite_deadline_thresholds_met"] == true);
    c.position = 912345;
    c.xrun = c.discontinuity = true;
    t.record(100000, &c, 456789);
    s = summary(t);
    require(s["calls"] == 1002 && s["samples"] == 1001 && s["dropped_samples"] == 1 &&
            s["maximum_ns"] == 100000 && s["complete_timing_coverage"] == false &&
            s["finite_deadline_thresholds_met"] == false &&
            s["maximum_callback_start_monotonic_ns"] == 456789 &&
            s["maximum_clock_known"] == true && s["maximum_clock"]["position"] == 912345 &&
            s["maximum_clock"]["xrun"] == true && s["maximum_clock"]["discontinuity"] == true);
    c.xrun = c.discontinuity = false;
    DurationTiming boundary(3);
    boundary.record(7999, &c);
    boundary.record(8000, &c);
    require(summary(boundary)["finite_deadline_thresholds_met"] == false);
    DurationTiming missing(2);
    missing.record(1, nullptr);
    missing.record(1, &c);
    require(summary(missing)["missing_periods"] == 1 &&
            summary(missing)["complete_timing_coverage"] == false);
    DurationTiming variable(2);
    variable.record(5000, &c);
    c.duration = 10;
    variable.record(10000, &c);
    require(summary(variable)["finite_deadline_thresholds_met"] == true &&
            summary(variable)["minimum_quantum"] == 1 &&
            summary(variable)["maximum_quantum"] == 10);
    DurationTiming cpu(2, true);
    cpu.record(10000, &c, 45000, 3000, true);
    cpu.record(20000, &c, 55000, 4000, true);
    auto cpuSummary = summary(cpu);
    require(cpuSummary["complete_cpu_coverage"] == true && cpuSummary["cpu_samples"] == 2 &&
            cpuSummary["maximum_cpu_ns"] == 4000 && cpuSummary["mean_cpu_ns"] == 3500 &&
            cpuSummary["maximum_wall_minus_cpu_ns"] == 16000 &&
            cpuSummary["maximum_callback_cpu_known"] == true &&
            cpuSummary["maximum_callback_cpu_ns"] == 4000);
    cpu.record(30000, &c, 65000, 35000, true); // Invalid CPU interval, plus fixed-store overflow.
    cpuSummary = summary(cpu);
    require(cpuSummary["complete_cpu_coverage"] == false && cpuSummary["cpu_clock_failures"] == 1 &&
            cpuSummary["complete_timing_coverage"] == false &&
            cpuSummary["maximum_callback_cpu_known"] == false &&
            cpuSummary["maximum_callback_cpu_ns"] == 0 &&
            cpuSummary["maximum_callback_start_monotonic_ns"] == 65000);
    DurationTiming cpuMissing(1, true);
    cpuMissing.record(1, &c);
    require(summary(cpuMissing)["complete_cpu_coverage"] == false &&
            summary(cpuMissing)["cpu_clock_failures"] == 1);
    require(summary(variable)["cpu_timing_requested"] == false &&
            summary(variable)["complete_cpu_coverage"] == false);
    using native_fixture::ThreadUsage;
    using native_fixture::usageDelta;
    const ThreadUsage before{1000, 2000, 5, 6, 7, 8, true};
    const ThreadUsage after{2000, 5000, 7, 6, 8, 11, true};
    const auto delta = usageDelta(before, after);
    require(delta.known && delta.userNs == 1000 && delta.systemNs == 3000 &&
            delta.minorFaults == 2 && delta.majorFaults == 0 && delta.voluntarySwitches == 1 &&
            delta.involuntarySwitches == 3 && !usageDelta({}, after).known);
    for (unsigned field = 0; field < 6; ++field) {
        auto backwards = after;
        switch (field) {
        case 0:
            backwards.userNs = before.userNs - 1;
            break;
        case 1:
            backwards.systemNs = before.systemNs - 1;
            break;
        case 2:
            backwards.minorFaults = before.minorFaults - 1;
            break;
        case 3:
            backwards.majorFaults = before.majorFaults - 1;
            break;
        case 4:
            backwards.voluntarySwitches = before.voluntarySwitches - 1;
            break;
        default:
            backwards.involuntarySwitches = before.involuntarySwitches - 1;
            break;
        }
        require(!usageDelta(before, backwards).known);
    }
    const auto liveBefore = native_fixture::threadUsageNow();
    const auto liveAfter = native_fixture::threadUsageNow();
    require(liveBefore.known && liveAfter.known && usageDelta(liveBefore, liveAfter).known);
    c.duration = 4; // 40us native period.
    c.monotonicNs = 100000;
    DurationTiming usage(2, true, true);
    usage.record(3000, &c, 100900, 2500, true, delta);
    c.monotonicNs = 150000;
    c.position = 123;
    const ThreadUsage later{1000, 1000, 0, 1, 0, 0, true};
    usage.record(2000, &c, 200000, 1500, true, later);
    auto u = summary(usage);
    require(u["complete_thread_usage_coverage"] == true && u["thread_usage_samples"] == 2 &&
            u["thread_usage_totals"]["system_ns"] == 4000 && u["maximum_system_ns"] == 3000 &&
            u["thread_usage_totals"]["minor_faults"] == 2 && u["maximum_major_faults"] == 1 &&
            u["maximum_callback_thread_usage"]["system_ns"] == 3000 &&
            u["maximum_callback_thread_usage"]["involuntary_switches"] == 3 &&
            u["cycle_context_samples"] == 2 && u["cycle_context_unknown"] == 0 &&
            u["callback_end_after_cycle_period_count"] == 1 &&
            u["maximum_callback_end_after_cycle_ns"] == 52000 &&
            u["maximum_cycle_callback_wall_ns"] == 2000 &&
            u["maximum_cycle_callback_cpu_ns"] == 1500 &&
            u["maximum_cycle_clock"]["position"] == 123 &&
            u["maximum_cycle_thread_usage"]["major_faults"] == 1);
    // Larger cycle context and wall maximum survive fixed-store overflow, with
    // missing CPU/resource measurements explicit rather than copied from earlier calls.
    c.monotonicNs = 200000;
    usage.record(50000, &c, 230000);
    u = summary(usage);
    require(u["complete_thread_usage_coverage"] == false && u["thread_usage_failures"] == 1 &&
            u["maximum_callback_thread_usage"]["known"] == false &&
            u["maximum_cycle_thread_usage"]["known"] == false &&
            u["maximum_cycle_callback_cpu_known"] == false &&
            u["maximum_callback_end_after_cycle_ns"] == 80000 && u["dropped_samples"] == 1 &&
            u["complete_timing_coverage"] == false);
    DurationTiming cycleBoundary(3);
    cycleBoundary.record(30000, &c, 210000); // Exactly at native period end.
    cycleBoundary.record(1, &c, 199999);     // Future/jittered cycle timestamp: unknown context.
    cycleBoundary.record(UINT64_MAX, &c, 200001); // Overflow: unknown context.
    u = summary(cycleBoundary);
    require(u["callback_end_after_cycle_period_count"] == 0 && u["cycle_context_samples"] == 1 &&
            u["cycle_context_unknown"] == 2 && u["thread_usage_requested"] == false &&
            u["complete_thread_usage_coverage"] == false);
    DurationTiming empty(1);
    require(summary(empty)["complete_timing_coverage"] == false);
}
