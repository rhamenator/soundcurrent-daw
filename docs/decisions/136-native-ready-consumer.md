# ADR136: consume monitor readiness before native content leases

Status: accepted for prepared composition; native factory remains disabled.
Date:2026-10-11.

## Decision

Opt in before SDK preparation with immutable finite warmup limits and a shared
prepared startup handoff. Admit actual descriptor/run/identity/geometry exactly
at context construction and stable binding. The processing owner independently
validates its source configuration against the same handoff.

Publish one original SDK render reading per pre-lease attempt, including missing
startup timestamps and native warmup. Use the independent actual QPC only as wait
clock. Processing owns confidence fitting, retained monitor preroll, actual
conversion and publication of immutable full-capacity readiness.

Acquire readiness and authorize one exact Content callback. Recheck terminal
state before pulling. Never return to startup silence after content begins;
live underflow faults. Preserve the immutable receipt after terminal states as
diagnostic evidence, not permission to consume. SDK lifecycle remains the owner
of actual GetBuffer/Release/Start/Stop results, finite budgets and queue drain.

Require context result observations to agree with original SDK end data. Keep
warmup attempts/commits separate from engine content and finite final slack.
Publish original end before render clock EOF. Missing native end remains owner
loss. Store no callback pointer/native backing in durable evidence.

Keep legacy no-hook behavior. Refuse new evidence in schema1.0 until explicit
version1.1 migration; dropping fields would make recovery evidence misleading.

## Qualification scope

[Checkpoint206](../206-native-ready-consumer.md) binds exact source to composed
real prepared processing with simulated SDK calls, original raw/clock/full-FIFO
comparison, concurrent ownership, failure tests, Linux sanitizers and Windows
compilation. Callback audit interposition is bounded coverage. Cross-compilation
and simulated success cannot establish native execution, installation, timing or
physical audio quality. Those gates precede factory activation.
