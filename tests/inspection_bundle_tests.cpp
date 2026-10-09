// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/inspection_bundle.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <source_location>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
int checks=0;
void check(bool yes,const char *message) { ++checks; if (!yes) throw std::runtime_error(message); }
std::string read(const std::filesystem::path &p) {
    std::ifstream s(p,std::ios::binary); return {std::istreambuf_iterator<char>(s),{}};
}
void write(const std::filesystem::path &p,std::string_view b) {
    std::ofstream s(p,std::ios::binary|std::ios::trunc); s.write(b.data(),std::streamsize(b.size()));
    check(bool(s),"Owned bundle fixture write failed");
}
template<class F> void refused(F f,std::source_location where=std::source_location::current()) {
    bool failed=false; try { f(); } catch(const ProjectError &) {failed=true;}
    ++checks;
    if (!failed) throw std::runtime_error("Unsafe/truncated bundle operation accepted at line "+std::to_string(where.line()));
}
#ifdef _WIN32
void legacyRenameProbe(const std::filesystem::path &root) {
    const auto parent=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES|FILE_TRAVERSE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(parent!=INVALID_HANDLE_VALUE,"Probe directory handle failed");
    const auto file=CreateFileW((root/L"legacy-probe.partial").c_str(),GENERIC_WRITE|DELETE,0,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) {CloseHandle(parent);throw std::runtime_error("Probe file failed");}
    check(file!=INVALID_HANDLE_VALUE,"Probe file handle failed");
    constexpr wchar_t name[]=L"legacy-probe.scinspect";
    alignas(FILE_RENAME_INFO) std::array<std::byte,512> bank{};
    auto *info=reinterpret_cast<FILE_RENAME_INFO *>(bank.data());
    info->RootDirectory=parent;info->FileNameLength=sizeof(name)-sizeof(wchar_t);
    std::memcpy(info->FileName,name,sizeof(name));
    const auto success=SetFileInformationByHandle(file,FileRenameInfo,info,
        static_cast<DWORD>(sizeof(FILE_RENAME_INFO)+sizeof(name)));
    const auto error=success ? 0 : GetLastError();
    FILE_DISPOSITION_INFO dispose{};dispose.DeleteFile=TRUE;
    const auto cleaned=SetFileInformationByHandle(file,FileDispositionInfo,&dispose,sizeof(dispose));
    CloseHandle(file);CloseHandle(parent);
    check(cleaned!=0,"Owned probe cleanup failed");
    std::cout<<"Win32 relative-root rename probe success="<<success<<" error="<<error
             <<"; original API observation, not a product fallback\n";
}
#endif
ImportInspectionReport fixture(ResourceLedger ledger) {
    // Explicit synthetic inventory; no foreign grammar is parsed by the loader.
    const std::string text="<REAPER_PROJECT 0.1 7.74\r\n <TRACK foreign\r\n NAME opaque-\xff\r\n >\r\n>";
    auto source=copyForeignSnapshot(text,ledger);
    const auto hash=hashForeignSnapshot(text,ledger);
    std::array<ForeignByteRange,5> lines{};
    for (std::size_t row=0,offset=0;row<5;++row) {
        const auto next=text.find('\n',offset);
        lines[row]={offset,next==text.npos ? text.size()-offset : next-offset+1}; offset+=lines[row].length;
    }
    using J=nlohmann::json;
    J nodes=J::array();
    const std::array<const char *,5> kinds{"block-open","block-open","data","block-close","block-close"};
    const std::array<ForeignByteRange,5> keys{{{1,14},{lines[1].begin+2,5},{lines[2].begin+1,4},
                                             {lines[3].begin+1,1},{lines[4].begin,1}}};
    for (std::size_t n=0;n<5;++n) {
        const auto extent=n==0 ? ForeignByteRange{0,text.size()} : n==1 ?
            ForeignByteRange{lines[1].begin,lines[3].begin+lines[3].length-lines[1].begin} : lines[n];
        nodes.push_back({{"index",n},{"kind",kinds[n]},{"parent",n==0 ? J(nullptr) : J(n==1||n==4 ? 0:1)},
            {"lineRange",{lines[n].begin,lines[n].length}},{"keyRange",{keys[n].begin,keys[n].length}},
            {"extentRange",{extent.begin,extent.length}},{"status","unverified"},{"originalBytesRetained",true}});
    }
    J j={{"protocol","sc-import-inspection-v1"},{"adapter","rpp-outline-v1"},{"sourceFormat","reaper-rpp"},
        {"workerPid",123456},{"source",{{"bytes",text.size()},{"sha256",std::string(hash.data(),hash.size())},
          {"storage","worker-memory-snapshot"},{"consistency","size-and-mtime-checked-not-atomic"}}},
        {"nativeCompatibility","unqualified"},{"semanticStatus","unverified"},
        {"writerVersion",{{"status","unverified"},{"headerRange",{0,lines[0].length}}}},
        {"root",0},{"nodes",nodes},{"complete",true}};
    auto encoded=j.dump();
    auto bank=ledger.reserve(inspectionProtocolCharge(encoded.size()));
    auto rows=ledger.reserve(inspectionRowsCharge(5)); auto parser=ledger.reserve(inspectionParserCharge(encoded.size()));
    return decodeInspectionReport(OwnedInspectionProtocol(std::move(bank),std::move(encoded)),std::move(source),
                                  hash,123456,ledger,std::move(rows),std::move(parser));
}
}
int main() {
    try {
        ResourceLedger capacity(65536);
        for (const auto length:{0u,1u,15u,16u,31u,32u,63u,64u}) {
            {
                auto grant=capacity.reserve(inspectionProtocolCharge(length));
                std::string bytes(length,'x');const auto actual=bytes.capacity();
                OwnedInspectionProtocol owned(std::move(grant),std::move(bytes));
                check(owned.bytes()==std::string(length,'x'),"Encoded capacity admission lost bytes");
                check(owned.chargedBytes()==actual && capacity.usage().reservedBytes==actual,
                      "Conservative capacity allowance did not retire to actual STL bank");
            }
            check(!capacity.usage().reservedBytes,"Encoded capacity borrower leaked credit");
        }
        refused([&] {
            std::string bytes(64,'x');auto grant=capacity.reserve(bytes.capacity()-1);
            OwnedInspectionProtocol insufficient(std::move(grant),std::move(bytes));
        });
        check(!capacity.usage().reservedBytes,"Unadmitted encoded constructor leaked credit");
        const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("sc-bundle-"+Id::generate().str());
        std::filesystem::create_directory(root);
#ifdef _WIN32
        legacyRenameProbe(root);
#endif
        ResourceLedger memory(8*1024*1024);
        {
            auto report=fixture(memory);
            check(memory.usage().reservedBytes==report.chargedBytes(),"Fixture retained decoder scratch");
            refused([&]{saveInspectionBundle({},report,memory);});
            const auto nulName=std::filesystem::path::string_type{std::filesystem::path("odd").native()}+
                std::filesystem::path::value_type(0)+std::filesystem::path("tail").native();
            refused([&]{saveInspectionBundle(root/std::filesystem::path(nulName),report,memory);});
            const auto target=root/std::filesystem::path(u8"Séance Ελληνικά.scinspect");
            const auto saved=saveInspectionBundle(target,report,memory);
            check(saved.bytes==std::filesystem::file_size(target),"Published byte count differs");
            const auto pristine=read(target);
            check(pristine.substr(0,8)=="SCIBND01","Container version header differs");
            {
                auto reopened=loadInspectionBundle(target,memory);
                check(reopened.source()==report.source() && reopened.protocol()==report.protocol(),
                      "Reopen lost opaque source/protocol bytes");
                check(reopened.workerPid()==123456 && reopened.root()==0 && reopened.nodes().size()==5,
                      "Recorded provenance/inventory changed");
                for (std::size_t n=0;n<5;++n) {
                    const auto &a=report.nodes()[n],&b=reopened.nodes()[n];
                    check(a.kind==b.kind && a.parent==b.parent && a.line==b.line && a.key==b.key && a.extent==b.extent,
                          "Structural range lost after reopen");
                }
                check(memory.usage().reservedBytes==report.chargedBytes()+reopened.chargedBytes(),
                      "Reopen retained container/decoder transient credit");
            }
            const auto moved=root/"relocated.scinspect"; std::filesystem::rename(target,moved);
            { auto relocated=loadInspectionBundle(moved,memory);check(relocated.source()==report.source(),"Relocation requires original project"); }
            refused([&]{saveInspectionBundle(moved,report,memory);});
            check(read(moved)==pristine,"Save replaced an existing valid bundle");
            const auto partial=root/"cancel.scinspect";
            InspectionBundleSaveOptions fail;
            fail.afterWrite=[](std::size_t){throw ProjectError(ErrorCode::Io,"Injected interrupted write");};
            refused([&]{saveInspectionBundle(partial,report,memory,{},fail);});
            check(!std::filesystem::exists(partial),"Interrupted write published partial destination");
            std::stop_source stop; fail.afterWrite={};fail.stop=stop.get_token();fail.beforePublish=[&]{stop.request_stop();};
            refused([&]{saveInspectionBundle(partial,report,memory,{},fail);});
            check(!std::filesystem::exists(partial),"Canceled flush published destination");
            fail.stop={}; fail.beforePublish=[&]{write(partial,"competing destination");};
            refused([&]{saveInspectionBundle(partial,report,memory,{},fail);});
            check(read(partial)=="competing destination","Publication overwrote concurrent destination");
            const auto bad=root/"corrupt.scinspect";
            for (const auto index:{std::size_t(0),std::size_t(8),std::size_t(16),std::size_t(24),
                                  std::size_t(32),std::size_t(32)+report.source().size(),pristine.size()-1}) {
                auto changed=pristine; changed[index]^=1; write(bad,changed);
                refused([&]{loadInspectionBundle(bad,memory);});
                check(memory.usage().reservedBytes==report.chargedBytes(),"Corrupt bundle leaked admission");
            }
            for (const auto size:{std::size_t(0),std::size_t(8),std::size_t(31),pristine.size()-1}) {
                write(bad,std::string_view(pristine).substr(0,size));refused([&]{loadInspectionBundle(bad,memory);});
            }
            write(bad,pristine+"trailing");refused([&]{loadInspectionBundle(bad,memory);});
            ResourceLedger tooSmall(512);
            refused([&]{loadInspectionBundle(moved,tooSmall);});
            check(!tooSmall.usage().reservedBytes,"Budget refusal leaked credit");
            ResourceLedger other(8*1024*1024);
            refused([&]{saveInspectionBundle(root/"scope.scinspect",report,other);});
            check(!std::filesystem::exists(root/"scope.scinspect"),"Wrong scope created destination");
            const auto dir=root/"existing-directory";std::filesystem::create_directory(dir);
            refused([&]{saveInspectionBundle(dir,report,memory);});
            check(std::filesystem::is_directory(dir),"Existing directory replaced");
#ifdef _WIN32
            for (const auto *name:{L"NUL.scinspect",L"con",L"x:ads",L"trailing.",L"trailing ",
                                  L"angle<name",L"pipe|name",L"wild?name",L"control\x01name"})
                refused([&]{saveInspectionBundle(root/std::filesystem::path(name),report,memory);});
#else
            const auto link=root/"link.scinspect";std::filesystem::create_symlink(moved.filename(),link);
            refused([&]{saveInspectionBundle(link,report,memory);});
            refused([&]{loadInspectionBundle(link,memory);});
            check(std::filesystem::is_symlink(link) && read(moved)==pristine,"Final symlink target changed");
#endif
            check(memory.usage().reservedBytes==report.chargedBytes(),"Save/failure banks leaked");
        }
        check(memory.usage().reservedBytes==0 && memory.usage().owners==0,"Result banks did not retire");
        std::filesystem::remove_all(root); // Exact owned fixture tree only.
        std::cout<<"Inspection bundle checks="<<checks<<"; synthetic originating PID, no native-suite compatibility claim\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
