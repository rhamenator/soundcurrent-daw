# Protected worker qualification fixtures

Original project-owned synthetic fixtures, GPL-3.0-only. These are unchanged
maintainer-observed producer reports; no product binaries or personal recordings.
They support receipt consistency/refusal tests, not authentication or DSP/native
audio replay. The producer tests and frozen prototype are in this repository.

## linux.json

- Producer source: `ba6ec5f5536bc6e7a0351d7711cbfae9edbc14d7`
- Source tree: `d92847af4d2ca0004f7376465d8878df97063bda`
- Observation: local committed-source run
- Original report SHA256: `cd97a639d19d37eed264b682ebfc29bbe8a6fdeabd621f6d4104c232cce3531c`
- Scope: 33 actual processes, 21 completed renders, 18 prototype PCM comparisons.
- Physical audio/full processing quality: unqualified.

## windows.json

- Producer source: `48aa3b1fbbf6ed213d5ebc57b0fdba8f60dbaac2`
- Source tree: `d92847af4d2ca0004f7376465d8878df97063bda`
- Observation: GitHub Actions 38037318873
- Original report SHA256: `dcd77ce3ea835311daa9ce3bb52890b46f8a56cbd9350f3e8603604d3c5dc2db`
- Scope: 33 actual processes, 21 completed renders, 18 prototype PCM comparisons.
- Physical audio/full processing quality: unqualified.


The shared package checker freezes the six original source WAV/PCM hash pairs
from these independently matching platform captures. Named comparisons must
bind those input pairs, channels, geometry and the actual prototype request.
Refusal requests must match all twelve distinct intended scenarios and exact
typed failure/nonpublication packets. These additional consistency checks do
not authenticate reports or replay filesystem/audio observations.
