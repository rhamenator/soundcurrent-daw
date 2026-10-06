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
    DurationTiming empty(1);
    require(summary(empty)["complete_timing_coverage"] == false);
}
