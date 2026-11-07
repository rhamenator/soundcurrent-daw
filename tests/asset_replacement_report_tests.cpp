// SPDX-License-Identifier: GPL-3.0-only
#include <optional>
#include <soundcurrent/wave_report.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
using namespace soundcurrent::daw;
namespace {
unsigned checks=0;
void check(bool value,const char *message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void refuses(F f,ErrorCode expected=ErrorCode::InvalidState) {
    bool caught=false;try {f();}catch(const ProjectError &e){caught=true;check(e.code()==expected,"Unexpected binding error");}
    check(caught,"Replacement binding accepted invalid input");
}
nlohmann::json fixture() {
    return {{"protocol","sc-approved-wave-validation-v2"},{"complete",true},{"relative","chosen.wav"},
        {"sourceBytes",100},{"sourceSha256",std::string(64,'a')},{"frames",4},{"decodedFrames",4},
        {"rateHz",48000},{"channels",2},{"containerId","riff"},{"extensible",false},{"encodingId",5},
        {"bitsPerSample",32},{"channelMask",0},{"peakLinear",3.25},{"bytesRead",200},{"ioOperations",10},{"workerPid",42}};
}
WaveCheckReport report(ResourceLedger memory,nlohmann::json value=fixture(),std::size_t pid=42) {
    auto bytes=value.dump();auto protocol=OwnedInspectionProtocol(memory.reserve(waveReportMaximumBytes),std::move(bytes));
    return decodeWaveCheckReport(std::move(protocol),"chosen.wav",pid,1024,memory,
        memory.reserve(waveReportValueCharge),memory.reserve(waveReportParserCharge));
}
}
int main() {
    try {
        ResourceLedger memory(2*1024*1024,"Replacement binding test"),foreign(2*1024*1024,"Other project scope");
        const auto baseline=memory.usage();
        Asset asset;asset.sha256=std::string(64,'a');asset.frames=4;asset.sampleRate=48000;asset.layout.channels=2;
        asset.relativePath="media/missing.wav";const auto original=asset;
        const AssetReplacementContext context{11,0,21,31}; // Initial revision zero remains usable.
        {
            AssetReplacementTicket ticket(asset,context,memory);auto decoded=report(memory);
            verifyAssetReplacementReport(ticket,decoded,asset,context,42,memory);++checks;
            for(unsigned field=0;field<4;++field) {
                auto stale=context;if(field==0)++stale.projectEpoch;if(field==1)++stale.modelRevision;
                if(field==2)++stale.request;
                if(field==3)++stale.job;
                refuses([&]{verifyAssetReplacementReport(ticket,decoded,asset,stale,42,memory);});
            }
            for(unsigned field=0;field<5;++field) {
                auto changed=asset;if(field==0)changed.id=Id::generate();if(field==1)changed.sha256[0]='b';
                if(field==2)++changed.frames;
                if(field==3)changed.sampleRate=44100;
                if(field==4)changed.layout.channels=1;
                refuses([&]{verifyAssetReplacementReport(ticket,decoded,changed,context,42,memory);});
            }
            for(const auto pid:{0U,43U})refuses([&]{verifyAssetReplacementReport(ticket,decoded,asset,context,pid,memory);});
            refuses([&]{verifyAssetReplacementReport(ticket,decoded,asset,context,42,foreign);});
            auto other=report(foreign);refuses([&]{verifyAssetReplacementReport(ticket,other,asset,context,42,memory);});
            AssetReplacementTicket wrongScope(asset,context,foreign);
            refuses([&]{verifyAssetReplacementReport(wrongScope,decoded,asset,context,42,memory);});
            for(unsigned field=0;field<4;++field) {
                auto value=fixture();if(field==0)value["sourceSha256"]=std::string(64,'b');
                if(field==1){value["frames"]=5;value["decodedFrames"]=5;}
                if(field==2)value["rateHz"]=44100;
                if(field==3)value["channels"]=1;
                auto wrong=report(memory,value);
                refuses([&]{verifyAssetReplacementReport(ticket,wrong,asset,context,42,memory);},ErrorCode::MediaMismatch);
            }
            std::stop_source stop;stop.request_stop();
            refuses([&]{verifyAssetReplacementReport(ticket,decoded,asset,context,42,memory,stop.get_token());},ErrorCode::Canceled);
            auto retained=std::move(ticket);refuses([&]{verifyAssetReplacementReport(ticket,decoded,asset,context,42,memory);});
            auto moved=std::move(decoded);refuses([&]{verifyAssetReplacementReport(retained,decoded,asset,context,42,memory);});
            verifyAssetReplacementReport(retained,moved,asset,context,42,memory);++checks;
            check(moved.audio().peak==3.25 && asset==original,"Verification changed content or asset state");
        }
        // Preserve the exact admitted child protocol, including harmless layout;
        // serializing parsed fields again would not satisfy this oracle.
        const auto wire=std::string(" ")+fixture().dump(2)+"\r\n";
        std::optional<OwnedInspectionProtocol> handoff;
        {
            auto source=decodeWaveCheckReport(OwnedInspectionProtocol(memory.reserve(waveReportMaximumBytes*2),wire),
                "chosen.wav",42,1024,memory,memory.reserve(waveReportValueCharge),memory.reserve(waveReportParserCharge));
            const auto before=memory.usage();
            refuses([&]{source.copyProtocol(foreign);});
            auto sibling=memory.child(1024*1024,"Foreign child scope");
            refuses([&]{source.copyProtocol(sibling);});
            std::stop_source stop;stop.request_stop();
            refuses([&]{source.copyProtocol(memory,stop.get_token());},ErrorCode::Canceled);
            memory.configure(before.reservedBytes+waveReportMaximumBytes*2-1);
            refuses([&]{source.copyProtocol(memory);},ErrorCode::ResourceLimit);
            check(memory.usage().reservedBytes==before.reservedBytes && memory.usage().owners==before.owners,
                "Failed protocol handoff retained credit");
            memory.configure(2*1024*1024);
            handoff.emplace(source.copyProtocol(memory));
            check(handoff->bytes()==wire && handoff->ownedBy(memory) && handoff->chargedBytes()>=wire.size(),
                "Protocol handoff reconstructed or lost original bytes/credit");
            auto moved=std::move(source);
            refuses([&]{source.copyProtocol(memory);});
            auto second=moved.copyProtocol(memory);check(second.bytes()==wire,"Moved report lost exact protocol");
        }
        check(handoff->bytes()==wire,"Protocol handoff borrowed retired report storage");
        handoff.reset();
        for(unsigned field=0;field<3;++field) {
            auto bad=context;if(field==0)bad.projectEpoch=0;if(field==1)bad.request=0;if(field==2)bad.job=0;
            refuses([&]{AssetReplacementTicket t(asset,bad,memory);});
        }
        for(const auto &digest:{std::string(63,'a'),std::string(64,'A'),std::string(64,'z')}) {
            auto bad=asset;bad.sha256=digest;refuses([&]{AssetReplacementTicket t(bad,context,memory);});
        }
        for(unsigned field=0;field<3;++field) {
            auto bad=asset;if(field==0)bad.frames=0;if(field==1)bad.sampleRate=0;if(field==2)bad.layout.channels=0;
            refuses([&]{AssetReplacementTicket t(bad,context,memory);});
        }
        ResourceLedger tiny(1,"Insufficient replacement admission");
        refuses([&]{AssetReplacementTicket t(asset,context,tiny);},ErrorCode::ResourceLimit);
        auto malformed=fixture();malformed["matchesAsset"]=true;refuses([&]{report(memory,malformed);});
        malformed=fixture();malformed["workerPid"]=43;refuses([&]{report(memory,malformed);});
        malformed=fixture();malformed["relative"]="other.wav";refuses([&]{report(memory,malformed);});
        const auto after=memory.usage();check(after.owners==baseline.owners && after.reservedBytes==baseline.reservedBytes,"Replacement binding leaked admission");
        std::cout<<checks<<" replacement report binding checks passed; stale context, identity, PID, scope, cancellation and retirement; no publication\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
