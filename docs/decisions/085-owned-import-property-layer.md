# ADR085: original owned source-property layer

Status: accepted for bounded source inspection, not native conversion.
Date: 2026-10-09 UTC.

## Context

Structural source inventories and portable bundles cannot express typed source
properties, missing values, ambiguity or property-specific losses. The original
REAPER7.82/Linux writer corpus now has independent save/reopen observations, but
neither the public lexer nor API is a complete native project schema. Destination
pan/fade/stretch/take processing remains separately unqualified.

## Options evaluated

1. Decode directly into Session: low initial integration cost, but collapses
   source gain layers and unsupported types, loses unknown state and confuses
   source units with destination semantics. Rejected.
2. Adopt a general RPP parser: possible syntax reuse, but no evaluated candidate
   establishes the required source-version semantic/loss contract. Would require
   a separate license, maintenance, Linux/Windows, admission and provenance review.
   Not selected solely from syntax support or library names.
3. Add an original C++20 source-property model above our owned outline: selected.
   Uses existing resource ledger/standard library and no new product dependency.
   Retains complete source/ranges and explicit unsupported/missing/unverified
   values until approved conversion exists. Portable plain-data output can be
   independently validated before Qt observes it.

## Decision and costs

Use immutable, move-construct-only ownership; stable field IDs; original-number
and byte-range values; bounded counts/token scratch; and shared payload admission.
Restrict scalar shapes to the observed single-take envelope. Exact foreign identity
is retained rather than installed as a destination UUID. No vendor code/algorithm,
new dependency, equalizer changes or proprietary assets are copied or linked.

New code and tests are original GPL-3.0-only. Existing nlohmann JSON is used only
by the corpus witness test; the product library depends on our structural/session
ledger code and C++20 facilities. Ordinary Linux and MSVC/MinGW implementation
differences, source-version drift, other native formats, full loss visibility and
conversion/render workflows remain maintenance/integration costs.

The model is a library checkpoint. Worker protocol, independent parent decoder,
Qt preview, portable persistence and approved destination conversion must follow.
No semantic compatibility promotion follows from decoding scalars or compilation.
