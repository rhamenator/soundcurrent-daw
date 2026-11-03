// SPDX-License-Identifier: GPL-3.0-only
#include <soundcurrent/restoration_record.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool good,const char *message){++checks;if(!good)throw std::runtime_error(message);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const ProjectError &){caught=true;}check(caught,"Invalid restoration record admitted");}
std::string hex(std::string_view value){constexpr char digits[]="0123456789abcdef";std::string out;for(unsigned char c:value){out+=digits[c>>4];out+=digits[c&15];}return out;}
nlohmann::json fixture(){return {{"schema","sc-asset-restoration-v1"},{"phase",1},
    {"operation","11111111-1111-4111-8111-111111111111"},{"asset","22222222-2222-4222-8222-222222222222"},
    {"projectEpoch",11},{"modelRevision",0},{"request",21},{"relativePathHex",hex("media/été-Κиїв/take.wav")},
    {"sourceSha256",std::string(64,'a')},{"sourceBytes",52},{"frames",4},{"rateHz",48000},{"channels",1},{"layoutKind",0},{"sourceRootsPersisted",false}};}
}
int main(){try{
    ResourceLedger memory(4*1024*1024,"Restoration records"),foreign(4*1024*1024);
    Asset asset;asset.id=Id("22222222-2222-4222-8222-222222222222");asset.relativePath="media/été-Κиїв/take.wav";
    asset.sha256=std::string(64,'a');asset.frames=4;asset.layout={LayoutKind::Mono,1};
    const Id operation("11111111-1111-4111-8111-111111111111");const AssetRestorationContext context{11,0,21};
    {
        auto planned=decodeAssetRestorationRecord(fixture().dump(),memory);
        verifyAssetRestorationRecord(planned,asset,context,operation,AssetRestorationPhase::Planned,memory);++checks;
        auto encoded=encodeAssetRestorationRecord(planned,AssetRestorationPhase::Published,memory);
        auto published=decodeAssetRestorationRecord(encoded.bytes(),memory);
        verifyAssetRestorationRecord(published,asset,context,operation,AssetRestorationPhase::Published,memory);++checks;
        check(published.data().relativePath==asset.relativePath && published.data().content.bytes==52,"Unicode/content record changed");
        for(unsigned field=0;field<11;++field){auto changed=asset;auto current=context;auto op=operation;auto phase=AssetRestorationPhase::Planned;
            if(field==0)++current.projectEpoch;
            if(field==1)++current.modelRevision;
            if(field==2)++current.request;
            if(field==3)changed.id=Id::generate();
            if(field==4)changed.relativePath="media/other.wav";
            if(field==5)changed.sha256[0]='b';
            if(field==6)++changed.frames;
            if(field==7)changed.sampleRate=44100;
            if(field==8)changed.layout={LayoutKind::Stereo,2};
            if(field==9)op=Id::generate();
            if(field==10)phase=AssetRestorationPhase::Published;
            refuses([&]{verifyAssetRestorationRecord(planned,changed,current,op,phase,memory);});
        }
        refuses([&]{verifyAssetRestorationRecord(planned,asset,context,operation,AssetRestorationPhase::Planned,foreign);});
        refuses([&]{encodeAssetRestorationRecord(planned,AssetRestorationPhase::Published,foreign);});
        refuses([&]{encodeAssetRestorationRecord(planned,static_cast<AssetRestorationPhase>(3),memory);});
        auto moved=std::move(planned);refuses([&]{encodeAssetRestorationRecord(planned,AssetRestorationPhase::Planned,memory);});
        verifyAssetRestorationRecord(moved,asset,context,operation,AssetRestorationPhase::Planned,memory);++checks;
    }
    const auto baseline=memory.usage();
    auto bad=[&](nlohmann::json j){refuses([&]{decodeAssetRestorationRecord(j.dump(),memory);});
        check(memory.usage().reservedBytes==baseline.reservedBytes && memory.usage().owners==baseline.owners,"Invalid record credit leaked");};
    const auto original=fixture();
    for(auto it=original.begin();it!=original.end();++it){auto j=original;j.erase(it.key());bad(j);}
    for(const auto *key:{"phase","projectEpoch","modelRevision","request","sourceBytes","frames","rateHz","channels","layoutKind"}){
        for(const auto &value:{nlohmann::json(-1),nlohmann::json(1.5),nlohmann::json(true),nlohmann::json("1")}){auto j=original;j[key]=value;bad(j);}
    }
    for(unsigned mode=0;mode<19;++mode){auto j=original;
        if(mode==0)j["schema"]="unknown";
        if(mode==1)j["phase"]=3;
        if(mode==2)j["projectEpoch"]=0;
        if(mode==3)j["request"]=0;
        if(mode==4)j["sourceBytes"]=0;
        if(mode==5)j["sourceBytes"]=UINT64_MAX;
        if(mode==6)j["frames"]=UINT64_MAX;
        if(mode==7)j["rateHz"]=384001;
        if(mode==8)j["channels"]=1025;
        if(mode==9)j["layoutKind"]=3;
        if(mode==10)j["sourceSha256"]=std::string(64,'A');
        if(mode==11)j["sourceSha256"]=std::string(63,'a');
        if(mode==12)j["relativePathHex"]=hex("../outside.wav");
        if(mode==13)j["relativePathHex"]=hex("media/CON.wav");
        if(mode==14)j["relativePathHex"]="zz";
        if(mode==15)j["relativePathHex"]=hex("media/\xff.wav");
        if(mode==16)j["sourceRootsPersisted"]=true;
        if(mode==17)j["sourceRoot"]="/foreign/root";
        if(mode==18)j["channels"]=2;
        bad(j);
    }
    const auto raw=original.dump();
    for(const auto &bytes:{"["+raw+"]",std::string("{\"schema\":\"sc-asset-restoration-v1\",")+raw.substr(1),
        std::string("{\"nested\":")+raw+"}",raw+"x",std::string(assetRestorationRecordMaximumBytes+1,' '),raw+std::string(1,'\0')})
        refuses([&]{decodeAssetRestorationRecord(bytes,memory);});
    std::stop_source stop;stop.request_stop();refuses([&]{decodeAssetRestorationRecord(raw,memory,stop.get_token());});
    ResourceLedger tiny(1);refuses([&]{decodeAssetRestorationRecord(raw,tiny);});
    check(memory.usage().reservedBytes==0 && memory.usage().owners==0 && tiny.usage().owners==0,"Restoration record ownership leaked");
    std::cout<<checks<<" strict restoration record checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
