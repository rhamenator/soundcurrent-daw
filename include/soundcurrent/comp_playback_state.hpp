// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "session.hpp"
namespace soundcurrent::daw {
// Control-side state/provenance helpers. No callback or filesystem work.
std::size_t compPlaybackSourcePayloadBytes(const CompPlaybackSourceState &);
const CompPlaybackSourceState *findCompPlaybackSource(const Session &,const Id &asset);
SourcePosition compPlaybackRawPosition(const CompPlaybackSourceData &,SourcePosition);
void validateCompPlaybackSources(const Session &);
// Used by the core validator to compare the declared map with its exact source
// lineage, including a previously assembled source when present.
void validateCompPlaybackRawMap(const CompPlaybackSourceData &,const CompPlaybackSourceData *previous);
} // namespace soundcurrent::daw
