// SPDX-License-Identifier: GPL-3.0-only
#include "native_controller_wait_evidence.hpp"
#include <atomic>
#include <sstream>
using namespace native_monitor_fixture;
namespace {
void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
struct Directory {
    std::filesystem::path root=std::filesystem::temp_directory_path()/utf8Path("sc-native-owner-é-日本-"+Id::generate().str());
    Directory(){std::filesystem::create_directory(root);}
    ~Directory(){if(std::uncaught_exceptions()){try{const auto t=root.generic_u8string();std::cerr<<"preserved_failure_project="<<std::string(reinterpret_cast<const char *>(t.data()),t.size())<<'\n';}catch(...){}return;}
        std::error_code error;std::filesystem::remove_all(root,error);}
};
}
int main(){try {
    Directory directory;auto session=makeOneTrackSession("Wait — Ελληνικά","Raw");
    session.tracks.front().layout={LayoutKind::Mono,1};ProjectStore(directory.root).save(session);
    auto spy=std::make_shared<Spy>();std::atomic<bool> entered{false},release{false};
    NativeMonitorController c({[&](const auto &){entered=true;while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return std::make_unique<FakeStreams>(spy);}});
    struct Unblock{std::atomic<bool> &release;~Unblock(){release=true;}} unblock{release};
    auto preparation=std::make_shared<NativeMonitorPreparation>();preparation->root=directory.root;
    preparation->options.route.runGeneration=99;preparation->options.route.firstOutput={1024};
    preparation->options.raw.project=session.id;preparation->options.raw.track=session.tracks.front().id;
    preparation->options.raw.layout={LayoutKind::Mono,1};
    NativeMonitorCommand prepare;prepare.kind=NativeMonitorCommandKind::Prepare;prepare.generation=99;prepare.preparation=preparation;
    check(c.submit(prepare)==NativeMonitorAdmission::Accepted,"Wait evidence preparation refused");
    controlAwait({c,spy.get(),"qualification/factory-entered"},[&]{return entered.load();});
    const auto before=c.snapshot();
    check(before->phase==NativeMonitorPhase::Preparing,"Held factory lost immutable Preparing phase");
    std::ostringstream log;
    struct Capture{std::streambuf *old;~Capture(){std::cerr.rdbuf(old);}} capture{std::cerr.rdbuf(log.rdbuf())};
    bool expired=false;
    try{controlAwait({c,spy.get(),"qualification/blocked-factory",73},[]{return false;});}
    catch(const std::runtime_error &e){expired=std::string_view(e.what()).find("original five seconds")!=std::string_view::npos;}
    std::cerr.rdbuf(capture.old);
    const auto text=log.str();constexpr std::string_view prefix="native_control_wait_expired=";
    check(expired && text.starts_with(prefix),"Original five-second expiry lost its observation");
    const auto observation=nlohmann::json::parse(text.substr(prefix.size()));
    check(observation.at("operation")=="qualification/blocked-factory" && observation.at("variant")==73 &&
        observation.at("elapsedLocalNs").get<std::uint64_t>()>=5000000000ULL && observation.at("limitMilliseconds")==5000 &&
        observation.at("controllerPhase")==unsigned(NativeMonitorPhase::Preparing) &&
        observation.at("controllerSnapshotImmutable")==true && observation.at("capturedBeforeUnwind")==true &&
        observation.at("resultPresent")==false && observation.at("sdk").at("serviceTupleCoherent")==true &&
        observation.at("sdk").at("rawPackets")==0 && observation.at("progress").at("rawAccepted")==0 &&
        c.snapshot()==before && !release.load(),"Expiry fabricated controller state, packets, timing or cancellation");
    spy->sdkObservationSequence=1;
    check(controlWaitObservation({c,spy.get(),"qualification/busy-sequence"},0).at("sdk").at("serviceTupleCoherent")==false,
        "Busy SDK publication fabricated coherence");spy->sdkObservationSequence=0;
    Spy tuple;
    std::jthread publisher([&]{for(std::uint64_t n=1;n<=2000;++n){tuple.sdkObservationSequence.fetch_add(1,std::memory_order_acq_rel);
        tuple.sdkRawPackets.store(n,std::memory_order_release);tuple.qpc.store(100000000+n*100000,std::memory_order_release);
        tuple.sdkStage.store(6,std::memory_order_release);tuple.sdkObservationSequence.fetch_add(1,std::memory_order_release);}});
    unsigned coherent=0;
    for(unsigned n=0;n<2000;++n){const auto r=controlWaitObservation({c,&tuple,"qualification/tuple"},0).at("sdk");
        if(r.at("serviceTupleCoherent").get<bool>()){++coherent;
            check(r.at("nowCoordinate100ns").get<std::uint64_t>()==100000000+r.at("rawPackets").get<std::uint64_t>()*100000,
                "Coherent tuple combined separate SDK publications");}}
    publisher.join();check(coherent>0,"Tuple observation had no stable publication");
    std::ostringstream failed;failed.exceptions(std::ios::badbit|std::ios::failbit);
    try{failed.setstate(std::ios::badbit);}catch(const std::ios_base::failure&){}
    reportControlWait(failed,{c,spy.get(),"qualification/failed-sink"},0,std::source_location::current());
    check(failed.bad(),"Failed evidence sink invented success");
    release=true;controlAwait({c,spy.get(),"qualification/prepare-released"},[&]{return c.snapshot()->phase==NativeMonitorPhase::Ready;});
    c.requestShutdown();controlAwait({c,spy.get(),"qualification/shutdown"},[&]{return c.snapshot()->phase==NativeMonitorPhase::Closed;});
    std::cout<<text;
    std::cout<<"controller_wait_evidence original5s=true before_unwind=true immutable_snapshot=true coherent_tuple=true busy_bounded=true failed_sink_contained=true simulated_sdk=true\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
