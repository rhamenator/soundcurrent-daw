// SPDX-License-Identifier: GPL-3.0-only
#include "media_io.hpp"
#include "windows_file_path.hpp"
#include <soundcurrent/approved_media.hpp>
#include <soundcurrent/project_store.hpp>
#include <array>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Existing media was overwritten");}
}
int main(){std::filesystem::path base;try{
    base=std::filesystem::temp_directory_path()/("sc-media-path-"+Id::generate().str());
    auto root=base;while(root.native().size()<320)root/=utf8Path("recording-é-日本-abcdefghijklmnopqrstuv");
    std::filesystem::create_directories(fileSystemApiPath(root));media_io::plainDirectory(root);
#ifdef _WIN32
    check(windowsFileApiPath(L"C:\\audio\\take.wav")==L"\\\\?\\C:\\audio\\take.wav","Drive file API path differs");
    check(windowsFileApiPath(L"\\\\server\\share\\take.wav")==L"\\\\?\\UNC\\server\\share\\take.wav","UNC file API path differs");
#endif
    unsigned long unprefixedError=0;
#ifdef _WIN32
    // Observe the old representation under this host's actual policy. Do not
    // alter registry/manifest settings or require the old call to fail.
    const auto probe=root/"unprefixed-control.partial";
    const auto handle=CreateFileW(probe.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)unprefixedError=GetLastError();
    else {CloseHandle(handle);std::filesystem::remove(fileSystemApiPath(probe));}
#endif
    const auto partial=root/"audio.partial",output=root/"audio.wav";
    {media_io::File f(partial,true);f.write("owned audio payload\n");f.flush();f.close();}
    media_io::publishMedia(partial,output);check(!std::filesystem::exists(fileSystemApiPath(partial)),"Published partial remained");
    {media_io::File f(output,false);check(f.readBounded(100)=="owned audio payload\n","Long media descriptor read differs");}
    refuses([&]{media_io::File f(output,true);});
    {media_io::File f(partial,true);f.write("replacement must not publish");f.flush();}
    refuses([&]{media_io::publishMedia(partial,output);});
    {media_io::File f(output,false);check(f.readBounded(100)=="owned audio payload\n","No-overwrite publication changed destination");}
    const auto journal=root/"intent.json";media_io::publishJournal(journal,"first\n");
    check(media_io::readJournal(journal)=="first\n","Long journal read failed");
    media_io::publishJournal(journal,"second\n");check(media_io::readJournal(journal)=="second\n","Long journal replacement failed");
    {media_io::JobLease writer(root,true);check(writer.status()==media_io::LeaseStatus::Held,"Long writer lease failed");
        media_io::JobLease contender(root,false);check(contender.status()==media_io::LeaseStatus::Busy,"Active long-path writer was ignored");}
    {media_io::JobLease reader(root,false);check(reader.status()==media_io::LeaseStatus::Held,"Long reader lease failed");}
    ResourceLedger ledger(1024*1024,"Long media fixture");
    {ApprovedMediaRoot approved(root,ledger);auto file=approved.open("audio.wav",100);std::array<char,20> text{};file.readAt(0,text);file.verifyUnchanged();check(std::string_view(text.data(),text.size())=="owned audio payload\n","Approved long-root read failed");}
    check(!ledger.usage().reservedBytes,"Long-root admission leaked payload");
    media_io::SampleHash hash;const std::string original="owned audio payload\n";
    hash.updateBytes({reinterpret_cast<const std::byte *>(original.data()),original.size()});
    check(hashMediaFile(output)==hash.digest(),"Long project-store media hash differs");
    auto session=makeOneTrackSession("Long project — 日本", "Audio");ProjectStore store(root);
    store.save(session);check(store.load()==session,"Long project did not reopen");
    const auto prior=session;session.name="Renamed long project";store.save(session);
    check(store.load()==session && store.loadPrevious()==prior,"Long project replacement or backup differs");
    auto interrupted=session;interrupted.name="Must not commit";SaveOptions options;
    options.beforePublish=[]{throw ProjectError(ErrorCode::Canceled,"Injected publication failure");};
    refuses([&]{store.save(interrupted,options);});check(store.load()==session,"Interrupted long save changed current project");

    const auto backupBeforeCopy=store.loadPrevious();
    const auto relocated=root.parent_path()/utf8Path("relocated-é-日本");
    std::filesystem::copy(fileSystemApiPath(root),fileSystemApiPath(relocated),std::filesystem::copy_options::recursive);
    check(ProjectStore(relocated).load()==session && ProjectStore(relocated).loadPrevious()==backupBeforeCopy,
        "Copied long project or backup changed");
    check(hashMediaFile(relocated/"audio.wav")==hash.digest(),"Copied long media bytes changed");
    check(media_io::readJournal(relocated/"intent.json")=="second\n","Copied long journal changed");
    check(store.load()==session && store.loadPrevious()==backupBeforeCopy && hashMediaFile(output)==hash.digest(),"Relocation copy changed original");

    const auto length=root.native().size();std::filesystem::remove_all(fileSystemApiPath(base));
    std::cout<<"{\"checks\":"<<checks<<",\"rootNativeUnits\":"<<length<<",\"unprefixedWin32Error\":"<<unprefixedError<<",\"relocationCopyVerified\":true,\"projectSavedReopened\":true,\"mediaHashVerified\":true,\"journalPublished\":true,\"overwriteRefused\":true,\"leaseConflictRefused\":true}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"; fixture retained at "<<base<<'\n';return 1;}}
