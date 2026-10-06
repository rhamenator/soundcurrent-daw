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
    t.record(100000, &c);
    s = summary(t);
    require(s["calls"] == 1002 && s["samples"] == 1001 && s["dropped_samples"] == 1 &&
            s["maximum_ns"] == 100000 && s["complete_timing_coverage"] == false &&
            s["finite_deadline_thresholds_met"] == false);
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
    DurationTiming empty(1);
    require(summary(empty)["complete_timing_coverage"] == false);
}
