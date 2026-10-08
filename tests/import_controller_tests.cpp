// SPDX-License-Identifier: GPL-3.0-only
#include "import_inspection_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTest>
#include <nlohmann/json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool value,const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void wait(F condition) {
    QElapsedTimer clock; clock.start();
    while (!condition()) { if (clock.elapsed()>10000) throw std::runtime_error("Controller wait deadline"); QTest::qWait(2); }
}
std::shared_ptr<const InspectionSnapshot> finish(ImportInspectionController &controller) {
    wait([&]{return !controller.snapshot()->busy;}); return controller.snapshot();
}
void retired(const ResourceLedger &memory) {
    check(memory.usage().owners==0 && memory.usage().reservedBytes==0,"Inspection resource credit leaked");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temporary; check(temporary.isValid(),"No owned temporary directory");
        const auto bytes=temporary.path().toUtf8().toStdString();
        const auto file=utf8Path(bytes)/utf8Path("été-Κиїв.rpp");
        const std::string original="<REAPER_PROJECT 0.1 7.74\n <TRACK foreign\n NAME preserved\n >\n>\n";
        { std::ofstream stream(file,std::ios::binary); stream<<original; }
        ResourceLedger memory(128*1024*1024,"Inspection acceptance");
        std::shared_ptr<const InspectionSnapshot> held;
        {
            InspectionOptions options; options.memory=memory;
            ImportInspectionController controller(options);
            check(controller.submit(file)==Admission::Accepted,"Inspection request not admitted");
            held=finish(controller);
            check(held->phase==InspectionPhase::Complete && held->childPid && held->childExit==0 && held->report,
                  "Real child result incomplete");
            check(held->report->source()==original && held->report->nodes().size()==5 &&
                  held->report->workerPid()==held->childPid,"Captured source/range/PID changed");
            check(memory.usage().reservedBytes==held->report->chargedBytes(),"Transient work credit retained");
            controller.requestShutdown(); wait([&]{return controller.snapshot()->closed;});
            check(memory.usage().reservedBytes==held->report->chargedBytes(),"Closed facade freed borrowed result");
        }
        check(held->report->source()==original,"Result did not survive controller facade");
        held.reset(); retired(memory);
        {
            std::atomic<bool> reached=false, release=false;
            InspectionOptions options; options.memory=memory;
            options.beforeSpawn=[&] { reached=true; while (!release) QThread::msleep(1); };
            ImportInspectionController controller(options);
            check(controller.submit(file)==Admission::Accepted,"Queued request failed");
            wait([&]{return reached.load();});
            check(controller.submit(file)==Admission::Full,"Concurrent request replaced admitted inspection");
            controller.requestCancel(); release=true;
            const auto canceled=finish(controller);
            check(canceled->phase==InspectionPhase::Canceled && !canceled->childPid && !canceled->childExit,
                  "Pre-spawn cancellation invented an execution");
            retired(memory);
        }
        {
            InspectionOptions options; options.memory=memory; options.program=QStringLiteral("/missing-owned-import-worker");
            ImportInspectionController controller(options);
            controller.submit(file); const auto failed=finish(controller);
            check(failed->phase==InspectionPhase::Fault && !failed->childPid && !failed->childExit,
                  "Missing executable produced a success/exit"); retired(memory);
        }
        const std::vector<std::function<void(nlohmann::json &)>> changes={
            [](auto &j){j["workerPid"]=1;}, [](auto &j){j["protocol"]="unknown";},
            [](auto &j){j["source"]["sha256"]=std::string(64,'0');},
            [](auto &j){j["source"]["bytes"]=1;}, [](auto &j){j["complete"]=false;},
            [](auto &j){j["extra"]=1;}, [](auto &j){j["nodes"][1]["status"]="converted";},
            [](auto &j){j["nodes"][1]["parent"]=1;},
            [](auto &j){j["nodes"][2]["keyRange"]={100000,1};},
            [](auto &j){j["nodes"][1]["extentRange"]={0,1};},
            [](auto &j){j["nodes"][0]["index"]=-1;},
            [](auto &j){j["nodes"][1]["lineRange"][1]=1;},
            [](auto &j){j["nodes"].erase(j["nodes"].end()-1);},
            [](auto &j){j["writerVersion"]["status"]="verified";}};
        for (const auto &change : changes) {
            InspectionOptions options; options.memory=memory;
            options.afterChild=[&](std::string &encoded,std::size_t) {
                auto json=nlohmann::json::parse(encoded); change(json); encoded=json.dump();
            };
            ImportInspectionController controller(options); controller.submit(file);
            const auto failed=finish(controller);
            check(failed->phase==InspectionPhase::Fault && failed->childPid && failed->childExit==0 && !failed->report,
                  "Invalid child protocol was published as an import result"); retired(memory);
        }
        for (unsigned fault=0;fault<3;++fault) {
            InspectionOptions options; options.memory=memory;
            options.afterChild=[&](std::string &encoded,std::size_t) {
                if (fault==0) encoded.insert(1,"\"root\":0,"); // Duplicate, never normalize.
                if (fault==1) encoded=std::string(32,'[')+"0"+std::string(32,']');
                if (fault==2) encoded.erase(encoded.size()-2);
            };
            ImportInspectionController controller(options); controller.submit(file);
            const auto failed=finish(controller);
            check(failed->phase==InspectionPhase::Fault && !failed->report,"Malformed/deep/duplicate protocol accepted");
            retired(memory);
        }
        {
            ResourceLedger tiny(1024);
            InspectionOptions options; options.memory=tiny;
            ImportInspectionController controller(options); controller.submit(file);
            const auto failed=finish(controller);
            check(failed->phase==InspectionPhase::Fault && failed->error==ErrorCode::ResourceLimit &&
                  !failed->childPid && !failed->childExit,"Budget failure activated a child"); retired(tiny);
        }
        std::ifstream input(file,std::ios::binary); const std::string after((std::istreambuf_iterator<char>(input)),{});
        for (unsigned mode=0;mode<3;++mode) {
            InspectionOptions options; options.memory=memory;
#ifdef _WIN32
            options.program=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-import-lifecycle-probe.exe");
#else
            options.program=QCoreApplication::applicationDirPath()+QStringLiteral("/sc-import-lifecycle-probe");
#endif
            options.childMemoryBytes=mode==2 ? 555555 : 444444;
            options.deadlineMilliseconds=mode==0 ? 100 : 2000;
            ImportInspectionController controller(options);
            controller.submit(file);
            if (mode==1) {
                wait([&]{return controller.snapshot()->childPid!=0;}); controller.requestCancel();
            }
            const auto result=finish(controller);
            check(result->childPid && result->childExit && !result->report && !result->busy,
                  "Stopping child was released without its terminal receipt");
            if (mode==0) check(result->timedOut && result->phase==InspectionPhase::Fault,"Deadline not retained");
            if (mode==1) check(result->phase==InspectionPhase::Canceled,"Live cancel did not retire exact child");
            if (mode==2) check(result->error==ErrorCode::ResourceLimit,"Flooded response exceeded admission silently");
            retired(memory);
        }
        check(after==original,"Inspection changed original project bytes");
        std::cout<<"Import controller checks="<<checks<<"; real child, synthetic project, native compatibility unqualified\n";
        return 0;
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
