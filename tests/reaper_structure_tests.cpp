// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/reaper_structure.hpp>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace soundcurrent::daw;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void empty(const ResourceLedger &ledger) {
    check(ledger.usage().owners == 0 && ledger.usage().reservedBytes == 0,
          "Foreign structure retained credit after refusal/retirement");
}
template<class F> void refused(F &&f, ErrorCode code) {
    bool caught = false;
    try { f(); }
    catch (const ProjectError &e) { caught = true; check(e.code() == code,"Unexpected refusal code"); }
    check(caught,"Unqualified foreign structure accepted");
}
void opaqueBytes() {
    // Original synthetic container, not a project exported by REAPER. Paths and
    // script/plugin-shaped payloads are bytes only; no semantic claim is made.
    const std::string original =
        "\xEF\xBB\xBF \r\n<REAPER_PROJECT 0.1 7.74 1\r\n"
        "  <TRACK {not-a-qualified-identity}\r\n"
        "    NAME \"Voix \xC3\xA9t\xC3\xA9\"\r\n"
        "    <ITEM\r\n      POSITION 1.25\r\n"
        "      <SOURCE WAVE\r\n        FILE \"../outside/not-opened.wav\"\r\n      >\r\n    >\r\n"
        "    <FXCHAIN\r\n      <UNMAPPED_PLUGIN vendor\r\n"
        "        opaque012345+/=\r\n        SCRIPT `do_not_execute()`\r\n      >\r\n    >\r\n"
        "  >\r\n>\r\n \t\r\n";
    ResourceLedger ledger(8*1024*1024,"Foreign inspection");
    {
        auto doc = inspectReaperStructure(original,{},ledger);
        check(doc.source() == original,"Opaque foreign bytes changed");
        check(doc.root() == 1,"BOM/blank lines lost root location");
        check(doc.nodes().size() == 19,"Lines were dropped or invented");
        check(doc.bytes(doc.nodes()[doc.root()].key) == "REAPER_PROJECT","Root token changed");
        std::size_t next = 0, opens = 0, closes = 0;
        for (std::size_t i=0;i<doc.nodes().size();++i) {
            const auto &node = doc.nodes()[i];
            check(node.line.begin == next,"Line byte inventory has a gap/overlap");
            check(doc.bytes(node.line) == std::string_view(original).substr(next,node.line.length),
                  "Line inventory changed source bytes");
            next += node.line.length;
            if (node.parent != ReaperStructureNode::noParent) {
                check(node.parent<i && doc.nodes()[node.parent].kind == ReaperLineKind::BlockOpen,
                      "Invalid structural parent");
                const auto extent = doc.nodes()[node.parent].extent;
                check(node.line.begin>=extent.begin && next<=extent.begin+extent.length,
                      "Child escaped retained block extent");
            }
            if (node.kind == ReaperLineKind::BlockOpen) {
                ++opens;
                check(node.extent.begin == node.line.begin && node.extent.length>=node.line.length,
                      "Block extent dropped opener");
                check(doc.bytes(node.extent).ends_with(">\r\n"),"Block extent dropped close line");
            }
            if (node.kind == ReaperLineKind::BlockClose) ++closes;
        }
        check(next == original.size() && opens == 6 && closes == 6,"Incomplete byte/block inventory");
        const auto before = ledger.usage();
        auto moved = std::move(doc);
        check(moved.source() == original && ledger.usage() == before,
              "Move duplicated ownership or changed retained source");
        check(moved.chargedBytes() == before.reservedBytes && before.owners == 1,
              "Final payload admission differs from owner credit");
        refused([&] { moved.bytes({original.size(),1}); },ErrorCode::InvalidState);
        refused([&] { moved.bytes({1,std::numeric_limits<std::size_t>::max()}); },ErrorCode::InvalidState);
    }
    empty(ledger);
}
void grammarAndLimits() {
    ResourceLedger ledger;
    const std::string valid = "<REAPER_PROJECT\n<UNKNOWN fields\nopaque\n>\n>";
    { auto doc = inspectReaperStructure(valid,{},ledger); check(doc.source()==valid,"Final line requires newline"); }
    empty(ledger);
    for (const std::string bad : {"", "<OTHER\n>\n", ">\n", "<REAPER_PROJECT\n", "<REAPER_PROJECT\n>\n>\n",
         "<REAPER_PROJECT\n>\n<REAPER_PROJECT\n>\n", "data\n<REAPER_PROJECT\n>\n",
         "<REAPER_PROJECT\n>\ndata\n", "<REAPER_PROJECT\n> junk\n", "<REAPER_PROJECT\n<\n>\n>\n",
         "<REAPER_PROJECT\n<invalid!\n>\n>\n", "<REAPER_PROJECT\n\"<QUOTED\"\n>\n",
         "<REAPER_PROJECT\n'>quoted'\n>\n", "<REAPER_PROJECT\n`<quoted`\n>\n"}) {
        refused([&] { inspectReaperStructure(bad,{},ledger); },ErrorCode::InvalidState); empty(ledger);
    }
    std::string nul = "<REAPER_PROJECT\nopaque"; nul.push_back('\0'); nul += "\n>\n";
    refused([&] { inspectReaperStructure(nul,{},ledger); },ErrorCode::InvalidState); empty(ledger);
    auto limits = ReaperStructureLimits{};
    limits.maximumInputBytes = valid.size()-1;
    refused([&] { inspectReaperStructure(valid,limits,ledger); },ErrorCode::ResourceLimit); empty(ledger);
    limits = {}; limits.maximumLines = 4;
    refused([&] { inspectReaperStructure(valid,limits,ledger); },ErrorCode::ResourceLimit); empty(ledger);
    limits = {}; limits.maximumDepth = 1;
    refused([&] { inspectReaperStructure(valid,limits,ledger); },ErrorCode::ResourceLimit); empty(ledger);
    limits = {}; limits.maximumLineBytes = 15;
    { auto doc = inspectReaperStructure("<REAPER_PROJECT\n>\n",limits,ledger); check(!doc.source().empty(),"Exact line limit rejected"); }
    empty(ledger);
    refused([&] { inspectReaperStructure("<REAPER_PROJECT1\n>\n",limits,ledger); },ErrorCode::InvalidState); empty(ledger);
    refused([&] { inspectReaperStructure(std::string(16,'x'),limits,ledger); },ErrorCode::InvalidState); empty(ledger);
    limits = {}; limits.maximumDepth = 0;
    refused([&] { inspectReaperStructure(valid,limits,ledger); },ErrorCode::InvalidState); empty(ledger);
    ResourceLedger exact;
    std::size_t retained = 0;
    { auto doc = inspectReaperStructure(valid,{},exact); retained = doc.chargedBytes(); }
    exact.configure(retained-1);
    refused([&] { inspectReaperStructure(valid,{},exact); },ErrorCode::ResourceLimit); empty(exact);
    // Final retained size alone cannot admit the transient parent stack.
    exact.configure(retained);
    refused([&] { inspectReaperStructure(valid,{},exact); },ErrorCode::ResourceLimit); empty(exact);
    exact.configure(retained+5*sizeof(std::size_t));
    { auto doc = inspectReaperStructure(valid,{},exact); check(doc.chargedBytes()==retained,"Scratch credit not retired"); }
    empty(exact);
}
void cancellation() {
    ResourceLedger ledger(96*1024*1024);
    std::stop_source stop;
    stop.request_stop();
    refused([&] { inspectReaperStructure("<REAPER_PROJECT\n>\n",{},ledger,stop.get_token()); },ErrorCode::Canceled);
    empty(ledger);
    // Cancel after admission (not merely before entry). The observer watches
    // shared owner accounting, with no production parser hooks or sleeps.
    std::string large = "<REAPER_PROJECT\n";
    const std::string line = "OPAQUE "+std::string(192,'x')+"\n";
    for (unsigned i=0;i<75000;++i) large += line;
    large += ">\n";
    std::stop_source during;
    std::atomic<bool> done{false}, sawOwner{false};
    std::jthread observer([&](std::stop_token threadStop) {
        while (!threadStop.stop_requested() && !done.load()) {
            if (ledger.usage().owners) { sawOwner=true; during.request_stop(); return; }
            std::this_thread::yield();
        }
    });
    bool canceled = false;
    try { auto doc = inspectReaperStructure(large,{},ledger,during.get_token()); }
    catch (const ProjectError &e) { canceled = e.code()==ErrorCode::Canceled; }
    done=true; observer.join();
    check(sawOwner && canceled,"Admitted foreign inspection ignored cancellation");
    empty(ledger);
}
}
static_assert(!std::is_copy_constructible_v<ReaperStructure>);
static_assert(std::is_nothrow_move_constructible_v<ReaperStructure>);
static_assert(!std::is_move_assignable_v<ReaperStructure>);
int main() {
    try {
        opaqueBytes(); grammarAndLimits(); cancellation();
        std::cout << "RPP structural checks=" << checks << "; synthetic only; semantic/native compatibility unqualified\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
