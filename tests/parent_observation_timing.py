# SPDX-License-Identifier: GPL-3.0-only
"""Bounded owned-test observations. No cross-process monotonic-clock inference."""
import time


class ObservationTimeline:
    LIMIT = 128

    def __init__(self, monotonic_ns=time.monotonic_ns, utc_ns=time.time_ns):
        self._monotonic = monotonic_ns
        self._utc = utc_ns
        self._events = []

    def mark(self, name):
        if not isinstance(name, str) or not name or len(name) > 80:
            raise ValueError("Invalid observation event name")
        if len(self._events) == self.LIMIT:
            raise ValueError("Observation event limit reached; original events retained")
        steady = self._monotonic()
        utc = self._utc()
        if not isinstance(steady, int) or steady < 0 or not isinstance(utc, int) or utc < 0:
            raise ValueError("Invalid observation clock result")
        if self._events and steady < self._events[-1]["monotonicNs"]:
            raise ValueError("Observation monotonic clock regressed")
        event = {"sequence": len(self._events), "event": name,
                 "monotonicNs": steady, "utcNs": utc}
        self._events.append(event)
        return dict(event)

    def snapshot(self):
        # Same-observer spans only. UTC may step; no UTC-derived duration or
        # comparison with C++ steady-clock origins/device/project timing.
        spans = []
        opened = {}
        for event in self._events:
            name = event["event"]
            if name.endswith(".begin"):
                opened.setdefault(name[:-6], []).append(event)
            elif name.endswith(".end") and opened.get(name[:-4]):
                begin = opened[name[:-4]].pop()
                spans.append({"event": name[:-4], "beginSequence": begin["sequence"],
                              "endSequence": event["sequence"],
                              "elapsedNs": event["monotonicNs"] - begin["monotonicNs"]})
        return {"format": "sc-parent-observation-timing", "schemaMajor": 1,
                "monotonicDomain": "python-test-observer-monotonic-ns",
                "utcDomain": "python-test-observer-unix-utc-ns",
                "events": [dict(event) for event in self._events], "spans": spans,
                "unfinished": [{"event": name, "beginSequence": event["sequence"]}
                               for name, events in opened.items() for event in events]}
