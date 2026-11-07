// SPDX-License-Identifier: GPL-3.0-only
#include "stretch_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <nlohmann/json.hpp>
#include "media_io.hpp"
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool good,const char *message){++checks;if(!good)throw std::runtime_error(message);}
template<class F>void wait(F f){QElapsedTimer timer;timer.start();while(!f()){if(timer.elapsed()>20000)throw std::runtime_error("Stretch controller test deadline");QTest::qWait(2);}}
auto finish(StretchController &c){wait([&]{return !c.snapshot()->busy;});return c.snapshot();}
void put(std::string &s,std::uint64_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)s+=char((v>>(i*8))&255);}
void write(const std::filesystem::path &p,std::string_view data){std::ofstream out(p,std::ios::binary|std::ios::trunc);out.write(data.data(),std::streamsize(data.size()));out.close();check(bool(out),"Cannot write owned stretch fixture");}
std::string read(const std::filesystem::path &p){std::ifstream in(p,std::ios::binary);return {(std::istreambuf_iterator<char>(in)),{}};}
template<class F>void refuses(F f){try{f();}catch(const ProjectError &){++checks;return;}throw std::runtime_error("Invalid render recovery was accepted");}
std::shared_ptr<const ControllerSnapshot> project(const std::filesystem::path &root,ResourceLedger memory){
    std::filesystem::create_directory(root);std::filesystem::create_directory(root/"media");
    std::string data;for(unsigned f=0;f<12000;++f)for(unsigned ch=0;ch<2;++ch){const auto x=float(1.5*std::sin(2*3.141592653589793*(ch?730:440)*f/48000));put(data,std::bit_cast<std::uint32_t>(x),4);}
    std::string bytes="RIFF";put(bytes,36+data.size(),4);bytes+="WAVEfmt ";put(bytes,16,4);put(bytes,3,2);put(bytes,2,2);put(bytes,48000,4);put(bytes,48000*8,4);put(bytes,8,2);put(bytes,32,2);bytes+="data";put(bytes,data.size(),4);bytes+=data;
    const auto relative="media/raw-été.wav";write(root/utf8Path(relative),bytes);
    auto s=makeOneTrackSession("Owned background stretch","Audio");Asset a;a.relativePath=relative;a.sha256=hashMediaFile(root/utf8Path(relative));a.frames=12000;a.layout={LayoutKind::Stereo,2};s.assets={a};
    s.tracks[0]=makeAudioTrack("Audio",a.layout,s.sampleRate);
    Clip clip;clip.assetId=a.id;clip.sourceFrame=17;clip.sourceTiming={1,2};clip.lengthFrames=8192;s.tracks[0].clips={clip};validate(s);
    auto p=std::make_shared<ControllerSnapshot>();p->session=SessionSnapshots(memory).copy(s);p->root=root;p->projectEpoch=3;return p;
}
StretchOptions options(ResourceLedger memory,QString program){StretchOptions o;o.memory=memory;o.program=std::move(program);o.policy.deadlineMilliseconds=10000;o.policy.maximumInputFrames=100000;o.policy.maximumOutputBytes=4*1024*1024;o.policy.maximumSourceBytes=4*1024*1024;return o;}
struct Release {std::atomic<bool> &value;~Release(){value=true;}};
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
    check(app.arguments().size()==2,"Expected actual stretch helper");const auto helper=app.arguments()[1];
    QTemporaryDir tmp;check(tmp.isValid(),"Cannot create native stretch test folder");const auto base=utf8Path(tmp.path().toUtf8().toStdString());
    ResourceLedger memory(512*1024*1024,"Stretch controller acceptance");auto source=project(base/utf8Path("été-Κиїв"),memory);
    const auto track=source->session->tracks[0].id,clip=source->session->tracks[0].clips[0].id;const auto original=*source->session;
    std::optional<Id> retained;
    {
        const auto plan=prepareClipStretch(original,track,clip,{3,2,0,true});const auto policy=options(memory,helper).policy;
        const auto baseline=memory.usage();
        auto nul=source->root.native();nul+=std::filesystem::path::value_type(0);nul+=std::filesystem::path("ignored").native();
        auto longName=source->root.native();longName.append(4097,std::filesystem::path::value_type('x'));
        const auto previousDirectory=std::filesystem::current_path();
        struct RestoreDirectory {std::filesystem::path value;~RestoreDirectory(){std::error_code error;std::filesystem::current_path(value,error);}} restore{previousDirectory};
        // The Windows runner's checkout and temp folder can be on different
        // drives. Use only this owned parent as cwd while exercising the API.
        std::filesystem::current_path(source->root.parent_path());
        const auto relative=source->root.filename();check(!relative.empty() && !relative.is_absolute(),"Owned relative-root fixture unavailable");
        for(const auto &root:{relative,std::filesystem::path(nul),std::filesystem::path(longName)}) {
            bool rejected=false;try{persistStretchSelection(root,original,plan,Id::generate(),policy,memory);}
            catch(const ProjectError &e){rejected=true;check(e.code()==ErrorCode::InvalidParameter,"Invalid publication root lost parameter classification");}
            check(rejected,"Invalid publication root was accepted");
            check(!std::filesystem::exists(source->root/"media"/"stretch-selections"),"Invalid root mutated owned selection directory");
            const auto after=memory.usage();check(after.owners==baseline.owners && after.reservedBytes==baseline.reservedBytes,"Invalid root retained publication credit");
        }
    }
    {
        std::atomic<bool> entered=false,release=false;auto o=options(memory,helper);
        o.beforeSpawn=[&]{entered=true;while(!release)QThread::msleep(1);};
        StretchController *owner=nullptr;
        bool childReleased=false;o.afterChild=[&]{
            childReleased=memory.usage().reservedBytes<256*1024*1024;
            const auto terminal=owner->snapshot();
            check(terminal->childPid && terminal->childExit==0 && !terminal->abnormalExit &&
                  terminal->busy && !terminal->result,
                  "Post-child observer saw stale exit state or premature verified result");
        };
        StretchController c(o);owner=&c;Release unlock{release};
        check(c.render(source,track,clip,{3,2,700000,true})==Admission::Accepted,"Render refused");
        const auto queued=c.snapshot();wait([&]{return entered.load();});
        check(!queued->selection->plan,"Published queued selection was mutated");
        check(memory.usage().reservedBytes>=256*1024*1024,"Whole child ceiling not admitted before spawn");
        check(c.render(source,track,clip,{})==Admission::Full && !c.clearResult(),"Busy job was replaced/cleared");
        release=true;const auto done=finish(c);
        if(done->phase!=StretchPhase::Complete)std::cerr<<"stretch phase="<<unsigned(done->phase)<<" error="<<(done->error?int(*done->error):-1)<<" pid="<<done->childPid<<" exit="<<done->childExit.value_or(-999)<<"\n";
        check(done->phase==StretchPhase::Complete && done->result && done->childPid && done->childExit==0 && !done->abnormalExit,"Actual background render failed");
        retained=done->selection->operation;
        check(childReleased && done->selection->plan && !queued->selection->plan,"Child credit retained after reap or selection changed");
        check(*source->session==original,"Supervisor mutated Session");
        auto edited=original;EditHistory h(edited);check(h.structural({done->result->edit()}),"Verified result not adoptable");
        check(edited.tracks[0].clips[0].lengthFrames==12288 && edited.tracks[0].clips[0].stretch->sourceOrigin==SourcePosition{17,1,2},"Render lost duration/raw phase");
        check(h.undo() && edited==original && h.redo(),"Supervised render Undo/Redo failed");
        check(c.clearResult() && c.snapshot()->phase==StretchPhase::Idle,"Terminal result not clearable");
        check(done->result->edit().rendered.frames==12288,"Externally held result retired early");
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});check(c.render(source,track,clip,{})==Admission::Closing,"Closed controller admitted work");
    }
    {
        // A new controller has no in-process result. Reconstruct solely from the
        // parent manifest and independently verified helper media.
        auto o=options(memory,helper);StretchRecoveryLimits l;l.render=o.policy;
        const auto manifest=source->root/"media"/"stretch-selections"/(retained->str()+".json");
        const auto pristine=read(manifest);const auto document=nlohmann::json::parse(pristine);
        check(document["format"]=="sc-stretch-selection-v1","Parent selection not persisted");
        const auto plan=prepareClipStretch(original,track,clip,{3,2,700000,true});
        auto wrongTarget=plan;wrongTarget.expectedClip.startFrame++;const auto refusedOperation=Id::generate();
        refuses([&]{persistStretchSelection(source->root,original,wrongTarget,refusedOperation,o.policy,memory,l);});
        check(!std::filesystem::exists(manifest.parent_path()/(refusedOperation.str()+".json")),"Refused selection was published");
        refuses([&]{persistStretchSelection(source->root,original,plan,*retained,o.policy,memory,l);});
        check(read(manifest)==pristine,"Duplicate operation overwrote manifest");
        const auto stray=manifest.parent_path()/(std::string(36,'g')+".json");const auto nil=manifest.parent_path()/"00000000-0000-0000-0000-000000000000.json";
        write(stray,"stray");write(nil,"nil");
        const auto isolated=inventoryStretchSelections(source->root,original,memory,l);
        check(isolated.size()==1 && isolated[0].status==StretchRecoveryStatus::Ready,"Malformed or nil selection name hid valid jobs");
        std::filesystem::remove(stray);std::filesystem::remove(nil);
        StretchController reopened(o);check(reopened.scan(source)==Admission::Accepted,"Restart inventory refused");
        const auto scan=finish(reopened);
        if(scan->inventory)for(const auto &v:*scan->inventory)if(v.status!=StretchRecoveryStatus::Ready)std::cerr<<"inventory status="<<int(v.status)<<" detail="<<v.diagnostic<<'\n';
        check(scan->inventory && scan->inventory->size()==1 && scan->inventory->at(0).status==StretchRecoveryStatus::Ready && !scan->result,"Restart scan attached or lost job");
        check(reopened.review(source,*retained)==Admission::Accepted,"Explicit restart review refused");
        const auto reviewed=finish(reopened);check(reviewed->recovered && reviewed->result && reviewed->selection->settings==StretchSettings{3,2,700000,true} && !reviewed->childPid,"Restart review changed controls or respawned child");
        auto adopted=original;EditHistory history(adopted);check(history.structural({reviewed->result->edit()}),"Reviewed restart result not adoptable");
        check(inspectStretchSelection(source->root,adopted,*retained,memory,l).status==StretchRecoveryStatus::Attached,"Attached job appeared new");
        ProjectStore(source->root).save(adopted);check(inspectStretchSelection(source->root,ProjectStore(source->root).load(),*retained,memory,l).status==StretchRecoveryStatus::Attached,"Saved attachment lost classification");
        {
            auto versioned=adopted;EditHistory versions(versioned);
            check(versions.structural({CreateTrackVersion{track,"Blank — Ελληνικά","Rendered — Україна",false}}),"Empty version was not created");
            check(versioned.tracks[0].clips.empty(),"Inactive render fixture still has active clips");
            const auto inactive=versioned.tracks[0].inactiveVersions[0].identity.id;
            const auto retainedState=versioned;
            const auto rendered=source->root/utf8Path(reviewed->result->edit().rendered.relativePath);
            const auto renderedHash=hashMediaFile(rendered);
            ProjectStore store(source->root);store.save(versioned);
            auto loaded=store.load();check(loaded==versioned,"Inactive render changed on reopen");
            const auto entry=inspectStretchSelection(source->root,loaded,*retained,memory,l);
            check(entry.status==StretchRecoveryStatus::Attached && !entry.verified,"Inactive render offered for adoption or marked stale");
            auto held=rendered;held+=".held";std::filesystem::rename(rendered,held);
            refuses([&]{store.verifyMedia(loaded);});
            std::filesystem::rename(held,rendered);store.verifyMedia(loaded);
            check(hashMediaFile(rendered)==renderedHash,"Missing-media restoration changed inactive render");
            check(versions.structural({RemoveTrackVersion{track,inactive}}),"Inactive version removal failed");
            check(inspectStretchSelection(source->root,versioned,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Unreferenced render still classified attached");
            check(hashMediaFile(rendered)==renderedHash,"Version removal deleted or modified media");
            check(versions.undo() && versioned==retainedState,"Version removal Undo lost inactive media reference");
            check(inspectStretchSelection(source->root,versioned,*retained,memory,l).status==StretchRecoveryStatus::Attached,"Undo did not restore inactive attachment");
            check(read(manifest)==pristine,"Version recovery modified original manifest");
        }
        check(history.undo() && inspectStretchSelection(source->root,adopted,*retained,memory,l).status==StretchRecoveryStatus::Ready,"Undo did not restore recoverable job");
        ProjectStore(source->root).save(original);
        auto stale=original;stale.tracks[0].clips[0].startFrame++;
        check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Changed target was accepted");
        stale=original;stale.id=Id::generate();check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Foreign project was accepted");
        stale=original;stale.tracks[0].clips.clear();check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Stale,"Missing target was accepted");
        stale=original;stale.tracks[0].name="Unrelated rename";stale.tracks[0].eq.bands[0].gainDb=3;
        check(inspectStretchSelection(source->root,stale,*retained,memory,l).status==StretchRecoveryStatus::Ready,"Unrelated controls invalidated render");
        const auto job=source->root/"media"/"derived"/retained->str();
        check(read(job/"selection.sha256")==hashMediaFile(manifest),"Original selection not bound to owned job");
        refuses([&]{bindStretchSelection(source->root,*retained,hashMediaFile(manifest),memory,l);});
        {media_io::JobLease writer(job,false,true);check(writer.status()==media_io::LeaseStatus::Held,"Cannot own live-writer fixture");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Active,"Held job was inspected/admitted");}
        auto malformed=[&](nlohmann::json value){write(manifest,value.dump());check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Altered parent selection admitted");write(manifest,pristine);};
        auto j=document;j["operation"]=Id::generate().str();malformed(j);
        const auto alias=prepareClipStretch(original,track,clip,{999998,666666,700000,true});
        j=document;j["request"]["timeNumerator"]=alias.anchor.settings.timeNumerator;j["request"]["timeDenominator"]=alias.anchor.settings.timeDenominator;
        check(stretchRenderKey(alias.source,alias.anchor,memory)==document.at("renderKey").get<std::string>(),"Rounded-ratio alias fixture differs from render key");malformed(j);
        j=document;j["request"]["timeNumerator"]=2;malformed(j);
        j=document;j["request"]["timeNumerator"]=3.0;malformed(j);
        j=document;j["request"]["contextEnabled"]=1;malformed(j);
        j=document;j["request"]["relative"]="../raw.wav";malformed(j);
        j=document;j["policy"]["version"]=true;malformed(j);
        j=document;j["extra"]=false;malformed(j);
        j=document;auto mini=nlohmann::json::parse(j["selection"].get<std::string>());mini["name"]="Unexpected unrelated snapshot";j["selection"]=mini.dump();malformed(j);
        write(manifest,"{\"format\":\"duplicate\","+pristine.substr(1));check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Duplicate selection fields accepted");write(manifest,pristine);
        j=document;j["extra"]=nlohmann::json::array();for(unsigned i=0;i<20;++i)j["extra"]=nlohmann::json::array({j["extra"]});malformed(j);
        const auto complete=read(job/"complete.json");write(job/"complete.json","{}");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Tampered completion accepted");write(job/"complete.json",complete);
        const auto intent=read(job/"intent.json");write(job/"intent.json","{}");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Tampered render intent accepted");write(job/"intent.json",intent);
        const auto binding=read(job/"selection.sha256");write(job/"selection.sha256",std::string(64,'0'));
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Tampered selection binding accepted");write(job/"selection.sha256",binding);
        std::filesystem::rename(job/"complete.json",job/"held-complete.json");check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Incomplete,"Partial job became complete");std::filesystem::rename(job/"held-complete.json",job/"complete.json");
        const auto raw=source->root/utf8Path("media/raw-été.wav");const auto rawBytes=read(raw);auto changed=rawBytes;changed.back()^=1;write(raw,changed);
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Changed source digest accepted");write(raw,rawBytes);
        const auto audio=job/"audio.wav";const auto audioBytes=read(audio);changed=audioBytes;changed.back()^=1;write(audio,changed);
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Changed rendered sample accepted");write(audio,audioBytes);
#ifndef _WIN32
        const auto held=manifest.parent_path()/"held-manifest.partial";std::filesystem::rename(manifest,held);std::filesystem::create_symlink(held,manifest);
        check(inspectStretchSelection(source->root,original,*retained,memory,l).status==StretchRecoveryStatus::Invalid,"Linked selection accepted");std::filesystem::remove(manifest);std::filesystem::rename(held,manifest);
#endif
        std::stop_source canceled;canceled.request_stop();refuses([&]{inventoryStretchSelections(source->root,original,memory,l,canceled.get_token());});
        ResourceLedger exhausted(1024,"Recovery refusal");refuses([&]{inventoryStretchSelections(source->root,original,exhausted,l);});check(exhausted.usage().owners==0,"Failed recovery leaked credit");
        auto bounded=l;bounded.maximumManifestBytes=1;refuses([&]{inventoryStretchSelections(source->root,original,memory,bounded);});
        bounded=l;bounded.maximumEntries=1;const auto extra=manifest.parent_path()/"interrupted.partial";write(extra,"partial");refuses([&]{inventoryStretchSelections(source->root,original,memory,bounded);});std::filesystem::remove(extra);
        reopened.requestShutdown();wait([&]{return reopened.snapshot()->closed;});
        check(*source->session==original,"Restart review changed immutable project");
    }
    {
        // A foreign/older project may retain more entries than new publication
        // admits. Pages bound work and memory without silently dropping jobs.
        auto paged=project(base/utf8Path("pages-été-Κиїв"),memory);
        const auto directory=paged->root/"media"/"stretch-selections";std::filesystem::create_directory(directory);
        {media_io::JobLease marker(directory,true);check(marker.status()==media_io::LeaseStatus::Held,"Cannot create page ownership fixture");}
        std::set<std::string> expected;
        for(unsigned i=0;i<257;++i){const auto id=Id::generate().str();expected.insert(id);write(directory/(id+".json"),"{bad");}
        write(directory/"interrupted.partial","partial");write(directory/"00000000-0000-0000-0000-000000000000.json","nil");
        const auto baseline=memory.usage().reservedBytes;
        {
            StretchRecoveryScan scan(paged->root,*paged->session,memory);std::set<std::string> seen;std::size_t examined=0,pages=0;
            for(;;){auto page=scan.next();++pages;examined+=page.examined;
                check(page.examined<=128 && page.manifestBytes<=8*1024*1024 && page.entries.size()<=128,"Page exceeded original bounds");
                check(memory.usage().reservedBytes-baseline<4*1024*1024,"Inventory memory grew beyond one page");
                for(const auto &v:page.entries)check(v.status==StretchRecoveryStatus::Invalid && !v.verified && seen.insert(v.operation.str()).second,"Malformed job accepted or duplicated across pages");
                if(!page.hasMore)break;
            }
            check(pages==3 && examined==259 && seen==expected,"Pagination omitted entries beyond the first 128");
            check(scan.next().entries.empty(),"Completed cursor repeated its last page");
        }
        check(memory.usage().reservedBytes==baseline,"Retired page cursor retained resources");
        {
            StretchRecoveryLimits one;one.maximumEntries=1;
            StretchRecoveryScan scan(paged->root,*paged->session,memory,one);check(scan.next().hasMore,"Expected continuing directory");
            write(directory/(Id::generate().str()+".json"),"changed");
            refuses([&]{scan.next();});refuses([&]{scan.next();});
        }
        {
            StretchRecoveryLimits one;one.maximumEntries=1;StretchRecoveryScan scan(paged->root,*paged->session,memory,one);
            check(scan.next().hasMore,"Expected publication continuation");
            {media_io::JobLease publisher(directory,false,true);check(publisher.status()==media_io::LeaseStatus::Held,"Scan held publication lock between pages");refuses([&]{scan.next();});}
            refuses([&]{scan.next();});
        }
        {
            StretchRecoveryLimits one;one.maximumEntries=1;StretchRecoveryScan scan(paged->root,*paged->session,memory,one);
            check(scan.next().hasMore,"Expected cancelable continuation");std::stop_source stop;stop.request_stop();
            try{scan.next(stop.get_token());throw std::runtime_error("Canceled page accepted");}catch(const ProjectError &e){check(e.code()==ErrorCode::Canceled,"Wrong page cancellation result");}
            refuses([&]{scan.next();});
        }
        {
            StretchRecoveryLimits one;one.maximumEntries=1;StretchRecoveryScan scan(paged->root,*paged->session,memory,one);
            check(scan.next().hasMore,"Expected replacement continuation");
            const auto held=directory.parent_path()/"held-selections";std::filesystem::rename(directory,held);std::filesystem::create_directory(directory);
            {media_io::JobLease marker(directory,true);}
            refuses([&]{scan.next();});std::filesystem::remove_all(directory);std::filesystem::rename(held,directory);
        }
        {
            const auto bytesRoot=base/"byte-pages";std::filesystem::create_directory(bytesRoot);std::filesystem::create_directory(bytesRoot/"media");
            const auto dir=bytesRoot/"media"/"stretch-selections";std::filesystem::create_directory(dir);{media_io::JobLease marker(dir,true);}
            for(unsigned i=0;i<3;++i)write(dir/(Id::generate().str()+".json"),std::string(512*1024,'x'));
            StretchRecoveryLimits bounds;bounds.maximumTotalBytes=512*1024;
            StretchRecoveryScan scan(bytesRoot,*paged->session,memory,bounds);std::size_t n=0;
            for(;;){auto page=scan.next();check(page.examined==1 && page.manifestBytes==512*1024,"Byte budget did not defer next entry");++n;if(!page.hasMore)break;}
            check(n==3,"Byte-bound pages lost manifests");
        }
        {
            auto o=options(memory,helper);StretchController c(o);check(c.scan(paged)==Admission::Accepted,"Controller paged scan refused");
            auto first=finish(c);check(first->recoveryPage && first->recoveryPage->hasMore && first->inventoryPage==1,"Controller lost continuation");
            auto stale=std::make_shared<ControllerSnapshot>(*paged);++stale->modelRevision;
            refuses([&]{c.nextScanPage(stale);});stale=std::make_shared<ControllerSnapshot>(*paged);++stale->projectEpoch;
            refuses([&]{c.nextScanPage(stale);});
            check(c.nextScanPage(paged)==Admission::Accepted,"Controller continuation refused");auto second=finish(c);
            check(second->inventoryPage==2 && second->recoveryPage && second->recoveryPage->hasMore,"Controller second page lost cursor");
            check(first->recoveryId && first->recoveryId==second->recoveryId,"Continuation changed recovery ownership token");
            std::set<std::string> seen;for(const auto &v:*first->inventory)seen.insert(v.operation.str());
            for(const auto &v:*second->inventory)check(seen.insert(v.operation.str()).second,"Controller duplicated first-page entries");
            c.requestCancel();refuses([&]{c.nextScanPage(paged);});wait([&]{return c.snapshot()->canceled;});
            check(c.scan(paged)==Admission::Accepted && finish(c)->inventoryPage==1,"Canceled scan failed to restart");
            write(directory/(Id::generate().str()+".json"),"changed again");
            check(c.nextScanPage(paged)==Admission::Accepted,"Changed directory refused before worker validation");
            const auto changed=finish(c);check(changed->phase==StretchPhase::Fault && !changed->inventory && !changed->result,"Changed inventory published stale page");
            check(c.clearResult(),"Faulted page could not be cleared");
            check(c.scan(paged)==Admission::Accepted && finish(c)->inventoryPage==1,"Cleared scan did not restart");
            check(*paged->session==*c.snapshot()->inventoryProject->session,"Page scan mutated project");
            c.requestShutdown();wait([&]{return c.snapshot()->closed;});
        }
    }
    {
        auto paged=project(base/"scoped-retirement",memory);const auto directory=paged->root/"media"/"stretch-selections";
        std::filesystem::create_directory(directory);{media_io::JobLease marker(directory,true);}
        for(unsigned i=0;i<129;++i)write(directory/(Id::generate().str()+".json"),"{bad");
        std::atomic<bool> entered=false,release=false;auto o=options(memory,helper);
        o.beforeSpawn=[&]{entered=true;while(!release)QThread::msleep(1);};
        StretchController c(o);Release unlock{release};const auto baseline=memory.usage().reservedBytes;
        check(c.scan(paged)==Admission::Accepted,"Scoped retirement scan refused");
        auto page=finish(c);const auto oldId=page->recoveryId;check(oldId && page->recoveryPage->hasMore,"Scoped retirement token missing");page.reset();
        check(c.requestRecoveryCancel(oldId),"Owning dialog could not retire cursor");wait([&]{return c.snapshot()->canceled;});
        check(memory.usage().reservedBytes==baseline,"Cursor retirement retained worker page/session credit");
        check(c.scan(paged)==Admission::Accepted,"New scoped scan refused");page=finish(c);const auto newId=page->recoveryId;
        check(newId!=oldId && !c.requestRecoveryCancel(oldId) && !c.snapshot()->canceled,"Stale dialog canceled newer inventory");page.reset();
        check(c.requestRecoveryCancel(newId),"New dialog could not retire its cursor");wait([&]{return c.snapshot()->canceled;});
        check(memory.usage().reservedBytes==baseline,"Second scoped retirement retained credit");
        check(c.render(source,track,clip,{1,1,0,false})==Admission::Accepted,"Newer render refused after recovery retirement");wait([&]{return entered.load();});
        check(!c.requestRecoveryCancel(oldId) && !c.requestRecoveryCancel(newId),"Stale recovery dialog targeted unrelated live render");
        release=true;const auto done=finish(c);check(done->result && !done->canceled && done->childExit==0,"Stale dialog interrupted newer render");
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});
    }
    {
        // Exercise real parent publication/helper completion beyond the previous
        // whole-directory ceiling, then verify every Ready job across pages.
        auto large=project(base/utf8Path("publication-été-Κиїв"),memory);const auto saved=*large->session;
        ProjectStore(large->root).save(saved);const auto savedBytes=read(large->root/"project.json");
        const auto rawPath=large->root/utf8Path(saved.assets[0].relativePath);const auto rawBytes=read(rawPath);
        const auto t=saved.tracks[0].id,cid=saved.tracks[0].clips[0].id;
        auto o=options(memory,helper);StretchController c(o);std::set<std::string> expected;
        std::size_t maximumRetained=0;
        for(unsigned i=0;i<129;++i){
            check(c.render(large,t,cid,{1,1,0,false})==Admission::Accepted,"Large project render publication refused");
            const auto done=finish(c);check(done->phase==StretchPhase::Complete && done->result && done->childExit==0,"Render beyond old publication cap failed");
            expected.insert(done->selection->operation.str());maximumRetained=std::max(maximumRetained,memory.usage().reservedBytes);
            check(done->result->edit().rendered.frames==8192 && *large->session==saved,"Large project render changed source or geometry");
        }
        check(expected.size()==129 && maximumRetained<64*1024*1024,"Repeated completed jobs accumulated controller credit");
        const auto directory=large->root/"media"/"stretch-selections";
        for(unsigned i=0;i<20;++i)write(directory/("owned-remnant-"+std::to_string(i)+".partial"),std::string(512*1024,'x'));
        // More than8MiB of unrelated retained entries cannot block this bounded
        // publication. Existing manifest bytes and raw input remain immutable.
        const auto existing=directory/(*expected.begin()+".json");const auto before=read(existing);
        const auto plan=prepareClipStretch(saved,t,cid,{1,1,0,false});const auto incomplete=Id::generate();
        check(persistStretchSelection(large->root,saved,plan,incomplete,o.policy,memory).size()==64,"Aggregate retained bytes blocked publication");
        check(read(existing)==before,"New publication rewrote previous manifest");
        {
            StretchRecoveryScan scan(large->root,saved,memory);std::set<std::string> seen;std::size_t examined=0,pages=0;
            for(;;){auto page=scan.next();++pages;examined+=page.examined;
                check(page.examined<=128 && page.manifestBytes<=8*1024*1024,"Completed inventory exceeded page bounds");
                for(const auto &v:page.entries){
                    if(v.operation==incomplete)check(v.status==StretchRecoveryStatus::Incomplete && !v.verified,"Unrendered selection was reviewable");
                    else {check(v.status==StretchRecoveryStatus::Ready && v.verified && expected.contains(v.operation.str()),"Completed job lost independent verification");check(seen.insert(v.operation.str()).second,"Completed job repeated across pages");}
                }
                if(!page.hasMore)break;
            }
            check(pages>=2 && examined==150 && seen==expected,"Paged inventory lost completed jobs or remnants");
        }
        const auto target=Id::generate();
        std::stop_source stop;stop.request_stop();refuses([&]{persistStretchSelection(large->root,saved,plan,target,o.policy,memory,{},stop.get_token());});
        check(!std::filesystem::exists(directory/(target.str()+".json")),"Canceled publication left destination");
        {media_io::JobLease reader(directory,false);check(reader.status()==media_io::LeaseStatus::Held,"Cannot hold publication read lease");refuses([&]{persistStretchSelection(large->root,saved,plan,target,o.policy,memory);});}
        check(!std::filesystem::exists(directory/(target.str()+".json")),"Busy publication wrote destination");
        auto small=StretchRecoveryLimits{};small.maximumManifestBytes=1;
        refuses([&]{persistStretchSelection(large->root,saved,plan,target,o.policy,memory,small);});
        check(!std::filesystem::exists(directory/(target.str()+".json")),"Oversized manifest was published");
        ResourceLedger exhausted(1024,"Publication refusal");refuses([&]{persistStretchSelection(large->root,saved,plan,target,o.policy,exhausted);});
        check(exhausted.usage().owners==0,"Failed publication retained credit");
        refuses([&]{persistStretchSelection(large->root,saved,plan,incomplete,o.policy,memory);});
        check(read(existing)==before && *large->session==saved,"Refused publications changed existing state");
        check(read(large->root/"project.json")==savedBytes && read(rawPath)==rawBytes && ProjectStore(large->root).load()==saved,"Saved project/raw changed across large publication workflow");
        check(c.review(large,Id(*expected.begin()))==Admission::Accepted,"Large inventory explicit Review refused");
        const auto reviewed=finish(c);check(reviewed->recovered && reviewed->result,"Completed job could not be reverified for explicit Review");
        auto edited=saved;EditHistory history(edited);check(history.structural({reviewed->result->edit()}) && history.undo() && edited==saved,"Large inventory Apply/Undo failed");
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});
    }
    {
        std::atomic<bool> entered=false,release=false;auto o=options(memory,helper);o.beforeSpawn=[&]{entered=true;while(!release)QThread::msleep(1);};
        StretchController c(o);Release unlock{release};c.render(source,track,clip,{});wait([&]{return entered.load();});c.requestCancel();release=true;
        const auto done=finish(c);check(done->phase==StretchPhase::Canceled && done->canceled && !done->childPid && !done->result,"Cancel before spawn failed");
        check(!std::filesystem::exists(source->root/"media"/"derived"/done->selection->operation.str()),"Canceled preparation created a job");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;bool activeSeen=false;
        o.afterReady=[&]{StretchRecoveryLimits l;l.render=o.policy;
            activeSeen=inspectStretchSelection(source->root,original,owner->snapshot()->selection->operation,memory,l).status==StretchRecoveryStatus::Active;
            owner->requestCancel();};StretchController c(o);owner=&c;
        c.render(source,track,clip,{});const auto done=finish(c);
        check(done->canceled && done->childPid && done->childExit && done->phase==StretchPhase::RecoveryRequired && !done->result,"Ready cancellation attached unverified audio");
        check(activeSeen,"Real live helper ownership was not detected");
        const auto job=source->root/"media"/"derived"/done->selection->operation.str();
        check(std::filesystem::exists(job/"intent.json") && !std::filesystem::exists(job/"complete.json"),"Incomplete canceled job not retained");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;
        o.afterReady=[&]{const auto op=owner->snapshot()->selection->operation;const auto manifest=source->root/"media"/"stretch-selections"/(op.str()+".json");write(manifest,read(manifest)+" ");};
        StretchController c(o);owner=&c;c.render(source,track,clip,{3,2,0,false});const auto done=finish(c);
        const auto job=source->root/"media"/"derived"/done->selection->operation.str();
        check(done->phase==StretchPhase::RecoveryRequired && !done->result && !std::filesystem::exists(job/"start.request") && !std::filesystem::exists(job/"selection.sha256"),"Changed pre-start selection acknowledged rendering");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;o.afterChild=[&]{owner->requestCancel();throw ProjectError(ErrorCode::Io,"Owned ambiguous post-exit fixture");};
        StretchController c(o);owner=&c;c.render(source,track,clip,{1,1,0,false});const auto done=finish(c);
        check(done->phase==StretchPhase::Complete && done->result && done->canceled && done->abnormalExit && done->error==ErrorCode::Io,"Committed result not inspected after ambiguous/canceled exit");
    }
    {
        auto o=options(memory,helper);o.policy.memoryBytes=4096ULL*1024*1024;bool spawned=false;o.beforeSpawn=[&]{spawned=true;};
        StretchController c(o);c.render(source,track,clip,{});const auto done=finish(c);
        check(done->phase==StretchPhase::Fault && done->error==ErrorCode::ResourceLimit && !done->childPid && !spawned,"Aggregate refusal launched a child");
    }
    {
        auto o=options(memory,helper);o.policy.maximumSourceBytes=1;bool spawned=false;o.beforeSpawn=[&]{spawned=true;};
        StretchController c(o);c.render(source,track,clip,{});const auto done=finish(c);
        check(done->phase==StretchPhase::Fault && done->error==ErrorCode::ResourceLimit && !done->childPid && !spawned,"Source byte refusal launched a child");
    }
    {
        auto o=options(memory,helper);StretchController *owner=nullptr;o.afterReady=[&]{owner->requestShutdown();};
        StretchController c(o);owner=&c;c.render(source,track,clip,{});const auto done=finish(c);wait([&]{return c.snapshot()->closed;});
        check(done->canceled && done->childPid && done->childExit && !done->result,"Active shutdown did not reap/cancel the child");
        check(c.render(source,track,clip,{})==Admission::Closing,"Active shutdown left admission open");
    }
    {
        auto o=options(memory,helper);o.policy.deadlineMilliseconds=100;o.afterReady=[]{QThread::msleep(200);};
        unsigned beats=0;QTimer tick;tick.setInterval(2);QObject::connect(&tick,&QTimer::timeout,[&]{++beats;});tick.start();
        StretchController c(o);c.render(source,track,clip,{});const auto done=finish(c);tick.stop();
        check(done->timedOut && done->childPid && done->childExit && !done->result && done->phase==StretchPhase::RecoveryRequired,"Deadline published unverified output or left child live");
        check(!std::filesystem::exists(source->root/"media"/"derived"/done->selection->operation.str()/"start.request"),"Expired preparation acknowledged a render start");
        check(beats>=2,"Main event loop stalled during child deadline handling");
    }
    {
        ProjectStore(source->root).save(*source->session);
        ProjectController canonical;ProjectCommand open{CommandKind::Open};open.path=source->root;
        check(canonical.submit(open)==Admission::Accepted,"Owned project Open refused");
        wait([&]{return canonical.snapshot()->session && canonical.snapshot()->io==IoOperation::None;});
        const auto before=canonical.snapshot();auto o=options(canonical.resourceLedger(),helper);
        StretchController c(o);check(c.render(before,track,clip,{3,2,0,false})==Admission::Accepted,"Canonical render refused");
        const auto done=finish(c);check(done->phase==StretchPhase::Complete && done->result,"Canonical render failed");
        auto command=[&](std::uint64_t id){ProjectCommand value{CommandKind::Structural};value.stretchResult=done->result;
            value.structuralGuard=StructuralGuard{before->projectEpoch,before->session->id,before->root};value.structuralRequest=id;return value;};
        auto reject=[&](ProjectCommand value){const auto id=value.structuralRequest;const auto unchanged=*canonical.snapshot()->session;
            check(canonical.submit(std::move(value))==Admission::Accepted,"Guard test queue refused");
            wait([&]{return canonical.snapshot()->structuralRejected.request==id;});
            check(canonical.snapshot()->structuralRejected.error==ErrorCode::InvalidState && *canonical.snapshot()->session==unchanged,"Guard refusal changed canonical project");};
        auto unbound=command(1);unbound.structuralGuard.reset();reject(std::move(unbound));
        auto wrongRoot=command(2);wrongRoot.structuralGuard->root/= "other";reject(std::move(wrongRoot));
        auto unchecked=command(3);unchecked.stretchResult.reset();unchecked.edits={done->result->edit()};reject(std::move(unchecked));
        auto foreign=command(4);auto verifiedElsewhere=verifyOwnedClipStretch(before->root,*done->selection->plan,done->selection->operation,o.policy,memory);
        foreign.stretchResult=std::make_shared<const VerifiedClipStretch>(std::move(verifiedElsewhere));reject(std::move(foreign));
        check(canonical.submit(command(5))==Admission::Accepted,"Verified canonical adoption refused");
        wait([&]{return canonical.snapshot()->structuralCompleted.request==5;});
        check(!canonical.snapshot()->structuralCompleted.error && canonical.snapshot()->session->tracks[0].clips[0].assetId==done->result->edit().rendered.id,"Verified render not canonically attached");
        reject(command(6)); // Same raw source, but the clip changed after adoption.
        check(canonical.submit({CommandKind::Undo})==Admission::Accepted,"Canonical Undo refused");
        wait([&]{return *canonical.snapshot()->session==*before->session;});
        check(canonical.submit(open)==Admission::Accepted,"Same-project reopen refused");
        wait([&]{return canonical.snapshot()->projectEpoch!=before->projectEpoch && canonical.snapshot()->io==IoOperation::None;});
        reject(command(7)); // Same root/project/clip IDs; a new epoch still refuses.
        c.requestShutdown();wait([&]{return c.snapshot()->closed;});
        canonical.requestShutdown();wait([&]{return canonical.snapshot()->closed;});
    }
    {
        auto fractional=project(base/utf8Path("protected-restart-été"),memory);
        auto integer=*fractional->session;integer.tracks[0].clips[0].sourceTiming={};
        auto raw=std::make_shared<ControllerSnapshot>();raw->session=SessionSnapshots(memory).copy(integer);raw->root=fractional->root;raw->projectEpoch=5;
        auto o=options(memory,helper);WarpSettings markers;markers.markers.push_back({Id::generate(),{4096,0,1},{6144,0,1}});
        std::optional<Id> op;
        {
            StretchController c(o);check(c.render(raw,integer.tracks[0].id,integer.tracks[0].clips[0].id,{3,2,0,true},{},markers)==Admission::Accepted,"Protected restart render refused");
            auto done=finish(c);check(done->result && done->selection->warp==markers,"Protected restart did not render");op=done->selection->operation;
        }
        StretchController recovered(o);check(recovered.review(raw,*op)==Admission::Accepted,"Protected restart review refused");
        auto done=finish(recovered);check(done->recovered && done->result && done->selection->warp==markers && !done->childPid,"Protected restart lost marker identity or respawned");
        const auto manifest=raw->root/"media"/"stretch-selections"/(op->str()+".json");const auto pristine=read(manifest);auto j=nlohmann::json::parse(pristine);
        j["request"]["warp"]["markers"][0]["output"][0]=6200;write(manifest,j.dump());StretchRecoveryLimits l;l.render=o.policy;
        check(inspectStretchSelection(raw->root,integer,*op,memory,l).status==StretchRecoveryStatus::Invalid,"Altered protected marker admitted");write(manifest,pristine);
        auto edited=integer;EditHistory h(edited);check(h.structural({done->result->edit()}) && h.undo() && edited==integer,"Protected restart adoption/Undo failed");
    }
    source.reset();check(memory.usage().owners==0,"Supervisor/result/session credit leaked");
    std::cout<<"stretch_controller_checks="<<checks<<" actual_owned_render=true whole_child_credit=true immutable_snapshots=true cancel_and_ambiguous_commit=true native_audio=false\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
