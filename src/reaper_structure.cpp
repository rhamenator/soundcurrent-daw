// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/reaper_structure.hpp>
#include <algorithm>

namespace soundcurrent::daw {
namespace {
constexpr auto noParent = ReaperStructureNode::noParent;
void canceled(std::stop_token stop) {
    if (stop.stop_requested()) throw ProjectError(ErrorCode::Canceled,"Foreign project inspection canceled");
}
void invalid(bool ok, const char *message) {
    if (!ok) throw ProjectError(ErrorCode::InvalidState,message);
}
ForeignByteRange nextLine(std::string_view source,std::size_t begin,std::size_t maximum) {
    // Bound each search so an unterminated line cannot defer cancellation by
    // scanning an entire arbitrarily enlarged input between worker checks.
    const auto available = source.size()-begin;
    const auto look = maximum < available ? maximum+1 : available;
    const auto newline = source.substr(begin,look).find('\n');
    const auto length = newline == std::string_view::npos ? look : newline+1;
    invalid(newline != std::string_view::npos || available <= maximum,"Foreign project line exceeds worker limit");
    const auto content = source.substr(begin,length-(newline != std::string_view::npos));
    invalid(content.find('\0') == std::string_view::npos,"Binary NUL in text project container");
    return {begin,length};
}
bool space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool tag(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}
}
std::string_view ReaperStructure::bytes(ForeignByteRange range) const {
    invalid(range.begin <= source_.size() && range.length <= source_.size()-range.begin,
            "Foreign source byte range outside retained document");
    return std::string_view(source_).substr(range.begin,range.length);
}
ReaperStructure inspectReaperStructure(std::string_view source, ReaperStructureLimits limits,
                                     ResourceLedger resources, std::stop_token stop) {
    canceled(stop);
    invalid(limits.maximumInputBytes && limits.maximumLines && limits.maximumLineBytes && limits.maximumDepth,
            "Invalid trusted foreign project limits");
    if (source.size() > limits.maximumInputBytes)
        throw ResourceLimitError("Foreign project input",source.size(),limits.maximumInputBytes);
    invalid(!source.empty(),"Empty foreign project container");
    std::size_t lineCount = 0;
    for (std::size_t begin=0;begin<source.size();) {
        canceled(stop); const auto line = nextLine(source,begin,limits.maximumLineBytes);
        if (++lineCount > limits.maximumLines)
            throw ResourceLimitError("Foreign project lines",lineCount,limits.maximumLines);
        begin += line.length;
    }
    const auto depthCapacity = std::min(lineCount,limits.maximumDepth);
    PayloadCharge charge("Foreign project structure",resources.usage().limitBytes);
    charge.add(sizeof(ReaperStructure)); charge.add(source.size()); charge.add(1);
    charge.add(lineCount,sizeof(ReaperStructureNode));
    const auto retained = charge.bytes(); charge.add(depthCapacity,sizeof(std::size_t));
    ReaperStructure result;
    result.lease_ = resources.reserve(charge.bytes());
    result.source_.assign(source); result.nodes_.resize(lineCount);
    source = result.source_; // Inventory the retained owner, not a later borrowed input view.
    bool rootClosed = false;
    {
        std::vector<std::size_t> parents; parents.reserve(depthCapacity);
        std::size_t index=0;
        for (std::size_t begin=0;begin<source.size();++index) {
            canceled(stop);
            auto &node = result.nodes_[index];
            node.line = nextLine(source,begin,limits.maximumLineBytes); node.extent = node.line;
            node.parent = parents.empty() ? noParent : parents.back();
            auto first = begin, end = begin+node.line.length;
            if (!begin && source.substr(0,3) == "\xEF\xBB\xBF") first += 3;
            while (first<end && space(source[first])) ++first;
            while (end>first && space(source[end-1])) --end;
            if (first == end) node.kind = ReaperLineKind::Blank;
            else {
                invalid(!((source[first] == '"' || source[first] == '\'' || source[first] == '`') &&
                          first+1<end && (source[first+1] == '<' || source[first+1] == '>')),
                        "Quoted block delimiter requires a qualified format adapter");
                if (source[first] == '<') {
                    node.kind = ReaperLineKind::BlockOpen;
                    std::size_t keyEnd = first+1;
                    while (keyEnd<end && !space(source[keyEnd])) ++keyEnd;
                    node.key = {first+1,keyEnd-first-1};
                    invalid(node.key.length && std::all_of(source.begin()+node.key.begin,
                        source.begin()+node.key.begin+node.key.length,tag),"Unqualified foreign block tag");
                    if (parents.empty()) {
                        invalid(!rootClosed && result.root_ == noParent && result.bytes(node.key) == "REAPER_PROJECT",
                                "Expected exactly one REAPER_PROJECT container");
                        result.root_ = index;
                    }
                    if (parents.size() == limits.maximumDepth)
                        throw ResourceLimitError("Foreign project nesting",parents.size()+1,limits.maximumDepth);
                    parents.push_back(index);
                } else if (source[first] == '>') {
                    invalid(end-first == 1 && !parents.empty(),"Unbalanced or unqualified block close");
                    node.kind = ReaperLineKind::BlockClose; node.key = {first,1};
                    auto &opened = result.nodes_[parents.back()];
                    opened.extent.length = begin+node.line.length-opened.line.begin;
                    parents.pop_back(); if (parents.empty()) rootClosed = true;
                } else {
                    invalid(!parents.empty(),"Data outside the foreign root container");
                    node.kind = ReaperLineKind::Data;
                    auto keyEnd = first;
                    while (keyEnd<end && !space(source[keyEnd])) ++keyEnd;
                    node.key = {first,keyEnd-first}; // Raw token only; no quote/parameter interpretation.
                }
            }
            begin += node.line.length;
        }
        invalid(parents.empty() && rootClosed && result.root_ != noParent,"Unterminated foreign project container");
        canceled(stop);
    } // Retire scratch before returning its memory credit, on the worker.
    result.lease_.resize(retained);
    return result;
}
} // namespace soundcurrent::daw
