// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_dispatch.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
std::filesystem::path utf8Path(std::string_view value){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()),value.size()));}
void check(bool good,const char *message){++checks;if(!good)throw std::runtime_error(message);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Stale/unadmitted restoration dispatched");}
void put(std::string &s,unsigned v,unsigned n){for(unsigned i=0;i<n;++i)s+=char((v>>(8*i))&255);}
std::string wave(){std::string b="RIFF";put(b,44,4);b+="WAVEfmt ";put(b,16,4);put(b,1,2);put(b,1,2);put(b,48000,4);put(b,96000,4);put(b,2,2);put(b,16,2);b+="data";put(b,8,4);for(unsigned v:{0,1,32767,32768})put(b,v,2);return b;}
void write(const std::filesystem::path &p,std::string_view b){std::ofstream f(p,std::ios::binary);f.write(b.data(),static_cast<std::streamsize>(b.size()));check(bool(f),"Fixture write failed");}
std::string read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);check(bool(f),"Fixture read failed");return {std::istreambuf_iterator<char>(f),{}};}
WaveCheckReport report(ResourceLedger memory,const Asset &a,std::uint64_t size){
    auto json=nlohmann::json({{"protocol","sc-approved-wave-validation-v2"},{"complete",true},{"relative","source.wav"},
        {"sourceBytes",size},{"sourceSha256",a.sha256},{"frames",4},{"decodedFrames",4},{"rateHz",48000},{"channels",1},
        {"containerId","riff"},{"extensible",false},{"encodingId",2},{"bitsPerSample",16},{"channelMask",0},
        {"peakLinear",1.0},{"bytesRead",size*2},{"ioOperations",10},{"workerPid",42}}).dump();
    OwnedInspectionProtocol bytes(memory.reserve(waveReportMaximumBytes),std::move(json));
    return decodeWaveCheckReport(std::move(bytes),"source.wav",42,1024,memory,memory.reserve(waveReportValueCharge),memory.reserve(waveReportParserCharge));
}
}
int main(){const auto fixture=std::filesystem::temp_directory_path()/utf8Path("sc-restore-Κиїв-")/Id::generate().str();try{
    std::filesystem::create_directories(fixture/"input");const auto bytes=wave();write(fixture/"input/source.wav",bytes);
    ResourceLedger memory(8*1024*1024,"Restoration dispatch"),foreign(8*1024*1024);
    {
        ApprovedMediaRoot approved(fixture/"input",memory);auto source=approved.open("source.wav",1024);
        Session model=makeOneTrackSession("Dispatch","Original");Asset a;a.relativePath="media/été-Κиїв/take.wav";a.frames=4;a.layout={LayoutKind::Mono,1};
        const auto hash=source.digest();a.sha256.assign(hash.begin(),hash.end());model.assets.push_back(a);const auto original=model;
        const RestorationOwnerContext context{{11,0,21,31},41,51};AssetReplacementTicket checked(a,context.checked,memory);
        auto decoded=report(memory,a,bytes.size());
        auto root=[&]{const auto p=fixture/Id::generate().str();std::filesystem::create_directory(p);return p;};
        const auto baseline=memory.usage();
        {
            const auto target=root();RestorationOwnerView current{model,target,context};
            RestorationDispatchTicket ticket(current,a.id,checked,decoded,42,Id::generate(),1024,memory);
            verifyRestorationDispatch(ticket,current,memory);++checks;
            check(ticket.chargedBytes()>0 && std::filesystem::is_empty(target),"Metadata binding performed I/O");
            for(unsigned mode=0;mode<18;++mode){auto changed=model;auto c=context;auto path=target;bool closing=false,io=false;
                if(mode==0)changed.id=Id::generate();
                if(mode==1)path=root();
                if(mode==2)++c.checked.projectEpoch;
                if(mode==3)++c.checked.modelRevision;
                if(mode==4)++c.checked.request;
                if(mode==5)++c.checked.job;
                if(mode==6)++c.dispatchJob;
                if(mode==7)++c.autosaveToken;
                if(mode==8)closing=true;
                if(mode==9)io=true;
                if(mode==10)changed.assets.clear();
                if(mode==11)changed.assets.push_back(a);
                if(mode==12)changed.assets[0].relativePath="media/other.wav";
                if(mode==13)changed.assets[0].sha256[0]='z';
                if(mode==14)++changed.assets[0].frames;
                if(mode==15)changed.assets[0].sampleRate=44100;
                if(mode==16)changed.assets[0].layout={LayoutKind::Discrete,1};
                if(mode==17)changed.assets[0].id=Id::generate();
                RestorationOwnerView stale{changed,path,c,closing,io};refuses([&]{verifyRestorationDispatch(ticket,stale,memory);});
            }
            refuses([&]{verifyRestorationDispatch(ticket,current,foreign);});std::stop_source stop;stop.request_stop();
            refuses([&]{verifyRestorationDispatch(ticket,current,memory,stop.get_token());});
            auto moved=std::move(ticket);refuses([&]{verifyRestorationDispatch(ticket,current,memory);});
            verifyRestorationDispatch(moved,current,memory);++checks;
        }
        for(unsigned mode=0;mode<7;++mode){const auto target=root();auto c=context;auto path=target;
            if(mode==0)c.autosaveToken=0;
            if(mode==1)c.dispatchJob=0;
            if(mode==2)c.checked.job=0;
            if(mode==3)c.checked.request=0;
            if(mode==4)c.checked.projectEpoch=0;
            if(mode==5)path="relative";
            if(mode==6)c.checked.modelRevision=1;
            RestorationOwnerView invalid{model,path,c};refuses([&]{RestorationDispatchTicket t(invalid,a.id,checked,decoded,42,Id::generate(),1024,memory);});
            check(std::filesystem::is_empty(target),"Invalid binding touched project root");
        }
        {
            const auto target=root();RestorationOwnerView current{model,target,context};
            for(auto pid:{0U,43U})refuses([&]{RestorationDispatchTicket t(current,a.id,checked,decoded,pid,Id::generate(),1024,memory);});
            refuses([&]{RestorationDispatchTicket t(current,a.id,checked,decoded,42,Id::generate(),1,memory);});
            refuses([&]{RestorationDispatchTicket t(current,a.id,checked,decoded,42,Id::generate(),1024,foreign);});
            ResourceLedger tiny(1);refuses([&]{RestorationDispatchTicket t(current,a.id,checked,decoded,42,Id::generate(),1024,tiny);});
            check(tiny.usage().owners==0 && foreign.usage().owners==0,"Rejected ticket credit leaked");
            RestorationDispatchTicket ticket(current,a.id,checked,decoded,42,Id::generate(),1024,memory);auto stale=context;++stale.dispatchJob;
            RestorationOwnerView other{model,target,stale};refuses([&]{DispatchedAssetRestoration::prepare(std::move(ticket),source,other);});
            check(std::filesystem::is_empty(target),"Stale preparation created staging");
        }
        for(unsigned mode=0;mode<3;++mode){const auto target=root();RestorationOwnerView current{model,target,context};
            RestorationDispatchTicket ticket(current,a.id,checked,decoded,42,Id::generate(),1024,memory);
            auto prepared=DispatchedAssetRestoration::prepare(std::move(ticket),source,current);auto stale=context;
            if(mode==0)++stale.dispatchJob;
            if(mode==1)++stale.autosaveToken;
            if(mode==2)++stale.checked.modelRevision;
            RestorationOwnerView other{model,target,stale};refuses([&]{prepared.publish(other);});refuses([&]{prepared.publish(current);});
            check(!std::filesystem::exists(target/"media"),"Stale publication created canonical directories");
        }
        {
            const auto target=root();RestorationOwnerView current{model,target,context};MediaCommitOutcome result;
            {
                RestorationDispatchTicket ticket(current,a.id,checked,decoded,42,Id::generate(),1024,memory);
                auto prepared=DispatchedAssetRestoration::prepare(std::move(ticket),source,current);auto moved=std::move(prepared);
                refuses([&]{prepared.publish(current);});result=moved.publish(current);refuses([&]{moved.publish(current);});
            }
            check(result.published && result.receiptPublished && !result.postCommitFlushFailed,"Current owner did not publish");
            check(read(target/utf8Path(a.relativePath))==bytes && model==original,"Dispatch changed source/model identity");
        }
        {
            const auto target=root();RestorationOwnerView current{model,target,context};MediaCommitOutcome result;std::stop_source stop;
            {
                RestorationDispatchTicket ticket(current,a.id,checked,decoded,42,Id::generate(),1024,memory);
                auto prepared=DispatchedAssetRestoration::prepare(std::move(ticket),source,current);
                result=prepared.publish(current,stop.get_token(),[&](StageBoundary at,std::uint64_t){if(at==StageBoundary::AfterCommitBeforeDirectoryFlush)stop.request_stop();});
            }
            check(result.published && result.receiptPublished && stop.stop_requested() && read(target/utf8Path(a.relativePath))==bytes,"Post-publication cancel erased dispatch truth");
        }
        check(memory.usage().reservedBytes==baseline.reservedBytes && memory.usage().owners==baseline.owners,"Dispatch ownership leaked");
    }
    check(memory.usage().owners==0 && memory.usage().reservedBytes==0,"Dispatch source/report credit leaked");
    std::filesystem::remove_all(fixture);std::cout<<checks<<" restoration dispatch owner checks passed\n";return 0;
}catch(const std::exception &e){const auto path=fixture.generic_u8string();std::cerr<<e.what()<<"\nRetained restoration fixture ";std::cerr.write(reinterpret_cast<const char *>(path.data()),static_cast<std::streamsize>(path.size()));std::cerr<<'\n';return 1;}}
