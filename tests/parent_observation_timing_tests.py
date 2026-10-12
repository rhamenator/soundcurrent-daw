#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clock-domain/retention checks; does not emulate Windows process execution."""
from parent_observation_timing import ObservationTimeline

checks = 0


def check(value):
    global checks
    checks += 1
    assert value


def refused(call):
    try:
        call()
    except ValueError:
        check(True)
    else:
        check(False)


def main():
    steady = iter([10, 20, 30, 50, 70, 70, 99])
    utc = iter([1000, 500, 600, 100, 200, 200, 201])
    timeline = ObservationTimeline(lambda: next(steady), lambda: next(utc))
    first = timeline.mark("scan.begin")
    first["monotonicNs"] = 100000  # Published event is an owned copy.
    timeline.mark("scan.begin")
    timeline.mark("scan.end")
    timeline.mark("scan.end")
    timeline.mark("pending.begin")
    timeline.mark("equal-time")
    result = timeline.snapshot()
    check([e["sequence"] for e in result["events"]] == list(range(6)))
    check(result["events"][0]["monotonicNs"] == 10)
    check([s["elapsedNs"] for s in result["spans"]] == [10, 40])
    check(result["unfinished"] == [{"event": "pending", "beginSequence": 4}])
    check(result["events"][3]["utcNs"] < result["events"][0]["utcNs"])
    result["events"].clear()
    check(len(timeline.snapshot()["events"]) == 6)
    timeline.mark("unmatched.end")
    check(len(timeline.snapshot()["spans"]) == 2)
    refused(lambda: timeline.mark(""))
    refused(lambda: timeline.mark("x" * 81))
    regression = ObservationTimeline(lambda: 2, lambda: 10)
    regression.mark("start")
    regression._monotonic = lambda: 1
    refused(lambda: regression.mark("regression"))
    check(len(regression.snapshot()["events"]) == 1)
    invalid = ObservationTimeline(lambda: -1, lambda: 10)
    refused(lambda: invalid.mark("negative"))
    check(not invalid.snapshot()["events"])
    full = ObservationTimeline(lambda: 10, lambda: 100)
    for n in range(128):
        full.mark(str(n))
    before = full.snapshot()
    refused(lambda: full.mark("over-capacity"))
    check(full.snapshot() == before)
    # A real same-observer span, without assuming any cross-process clock origin.
    actual = ObservationTimeline()
    actual.mark("observation.begin")
    actual.mark("observation.end")
    check(actual.snapshot()["spans"][0]["elapsedNs"] >= 0)
    print(f"{checks} parent observation clock/domain/retention checks passed; Windows execution not claimed")


if __name__ == "__main__":
    main()
