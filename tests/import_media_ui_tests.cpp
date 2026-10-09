// SPDX-License-Identifier: GPL-3.0-only
#include "import_inspection_dialog.hpp"
#include "import_media_dialog.hpp"
#include "localization.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTableView>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>
#include <QLocale>
#include <fstream>
#include <iostream>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace {
unsigned checks=0;
void check(bool b,const char *s) {++checks;if (!b) throw std::runtime_error(s);}
template<class F> void wait(F fn) {QElapsedTimer c;c.start();while (!fn()) {if (c.elapsed()>10000) throw std::runtime_error("Media UI test deadline");QTest::qWait(2);}}
void close(ImportInspectionDialog &d) {d.close();wait([&]{return d.retired() && !d.isVisible();});}
void write(const std::filesystem::path &p,std::string_view s) {std::ofstream out(p,std::ios::binary);out<<s;check(bool(out),"Cannot write owned project");}
std::string project(std::string fields,std::string type="WAVE") {return "<REAPER_PROJECT 0.1 7.82\n <TRACK\n  <ITEM\n   <SOURCE "+type+"\n"+fields+"   >\n  >\n >\n>\n";}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);i18n::Runtime language(QStringLiteral("en"),QStringLiteral("en_US"));
    try {
        check(language.catalogLoaded(),"English media UI catalog not loaded");
        check(QCoreApplication::translate("ImportMediaModel","%n channel(s)",nullptr,1)==QStringLiteral("1 channel") &&
              QCoreApplication::translate("ImportMediaModel","%n channel(s)",nullptr,2)==QStringLiteral("2 channels"),"English channel plural forms wrong");
        QTemporaryDir tmp;check(tmp.isValid(),"No owned UI test folder");const auto root=utf8Path(tmp.path().toUtf8().toStdString());
        const auto file=root/"source.rpp";const auto original=project("    FILE \"media/mono.wav\"\n");write(file,original);
        const auto corpus=std::filesystem::path(SC_IMPORT_CORPUS_ROOT);ResourceLedger memory(128*1024*1024,"Media UI acceptance");
        InspectionOptions options;options.memory=memory;
        {
            ImportInspectionDialog inspector(nullptr,options);inspector.show();check(inspector.inspect(file),"Project inspection refused");
            wait([&]{return !inspector.snapshot()->busy;});check(inspector.snapshot()->report && inspector.snapshot()->report->source()==original,"Source report missing");
            auto *media=inspector.openMediaCheck();check(media!=nullptr,"Media button did not open checklist");
            auto *table=media->findChild<QTableView *>(QStringLiteral("importMediaTable"));
            check(table && table->model()->rowCount()==1,"Source-file media inventory wrong");
            check(!media->snapshot()->childPid && !media->snapshot()->selection,"Opening checklist accessed media automatically");
            check(!inspector.inspect(file),"A different source was admitted under a live media dialog");
            table->selectRow(0);unsigned pulses=0;QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++pulses;});heartbeat.start();
            check(media->checkReference(0,corpus),"Explicit media folder/reference not admitted");wait([&]{return !media->snapshot()->busy;});heartbeat.stop();
            check(pulses>0,"GUI stopped servicing events during media work");
            check(media->snapshot()->phase==WaveCheckPhase::Complete && media->snapshot()->report->audio().frames==96000,"Actual corpus media not validated");
            check(table->model()->data(table->model()->index(0,0)).toString()==QStringLiteral("media/mono.wav"),"Original media reference rewritten");
            check(media->checkReference(0,root),"Missing-media diagnostic refused");wait([&]{return !media->snapshot()->busy;});
            check(media->snapshot()->error==ErrorCode::MissingMedia,"Missing-media status lost");
            check(media->checkReplacement(0,corpus/"media/stereo.wav"),"Explicit replacement refused");wait([&]{return !media->snapshot()->busy;});
            check(media->snapshot()->phase==WaveCheckPhase::Complete && media->snapshot()->report->audio().channels==2 && media->snapshot()->selection->selectedFilename,"Selected replacement not checked");
            check(table->model()->data(table->model()->index(0,0)).toString()==QStringLiteral("media/mono.wav"),"Replacement altered original token");
            auto *clear=media->findChild<QPushButton *>(QStringLiteral("clearMediaChecks"));
            wait([&]{return clear->isEnabled() && table->model()->data(table->model()->index(0,2)).toString()==QStringLiteral("Checked snapshot");});
            check(table->model()->data(table->model()->index(0,3)).toString().contains(QStringLiteral("2 channels")),"Channel format was not displayed");
            check(!table->model()->data(table->model()->index(0,4)).toString().isEmpty(),"Sample peak was not displayed");
            if (argc>1) check(media->grab().save(QString::fromLocal8Bit(argv[1])),"Media screenshot save failed");
            clear->click();
            check(!media->snapshot()->report && !media->snapshot()->selection,"Clearing local choices retained active report");
            const auto bundle=root/"saved.scinspect";check(inspector.saveInspection(bundle),"Source-only inspection save refused");wait([&]{return !inspector.snapshot()->busy;});
            check(inspector.snapshot()->phase==InspectionPhase::Complete,"Inspection save failed");
            media->close();wait([&]{return media->retired();});
            check(inspector.openInspection(bundle),"Saved inspection reopen refused");wait([&]{return !inspector.snapshot()->busy;});
            auto *reopened=inspector.openMediaCheck();check(reopened && !reopened->snapshot()->selection && !reopened->snapshot()->childPid,"Reopening granted historical media access");
            {
                WaveCheckOptions slow;slow.memory=memory;slow.deadlineMilliseconds=2000;
                slow.program=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
                    QStringLiteral("/sc-import-lifecycle-probe.exe");
#else
                    QStringLiteral("/sc-import-lifecycle-probe");
#endif
                ImportMediaDialog live(nullptr,inspector.snapshot()->report,slow);live.show();
                check(live.checkReference(0,corpus),"Live close fixture refused");
                wait([&]{return live.snapshot()->childPid!=0;});
                QElapsedTimer closeTime;closeTime.start();live.close();
                check(closeTime.elapsed()<100 && live.isVisible(),"Live close blocked GUI or retired child prematurely");
                wait([&]{return live.retired() && !live.isVisible();});
                check(live.snapshot()->childExit.has_value(),"Media window closed before child terminal exit");
            }
            close(inspector);
        }
        check(memory.usage().owners==0 && memory.usage().reservedBytes==0,"Closed media UI retained credits");
        for (unsigned mode=0;mode<4;++mode) {
            const auto text=mode==0 ? project("    FILE a.wav\n    FILE b.wav\n") : mode==1 ? project("") :
                mode==2 ? project("    FILE media/mono.wav\n","MIDI") : project("    FILE \"<img src='x'>.wav\"\n");
            write(file,text);ImportInspectionDialog inspector(nullptr,options);inspector.show();inspector.inspect(file);wait([&]{return !inspector.snapshot()->busy;});
            auto *media=inspector.openMediaCheck();check(media!=nullptr,"Source evidence checklist missing");auto *table=media->findChild<QTableView *>();
            if (mode==0) {check(table->model()->rowCount()==2 && !media->checkReplacement(0,corpus/"media/mono.wav") && !media->checkReplacement(1,corpus/"media/mono.wav"),"Duplicate occurrence selected implicitly");}
            if (mode==1) {check(!media->checkReference(0,corpus) && media->checkReplacement(0,corpus/"media/mono.wav"),"Missing original reference cannot be explicitly replaced");wait([&]{return !media->snapshot()->busy;});check(media->snapshot()->phase==WaveCheckPhase::Complete,"Missing-reference replacement failed");}
            if (mode==2) check(table->model()->rowCount()==0 && !media->checkReplacement(0,corpus/"media/mono.wav"),"Unsupported source was reinterpreted as WAVE");
            if (mode==3) {check(table->model()->data(table->model()->index(0,0)).toString().startsWith(QStringLiteral("<img")),"Foreign label not displayed literally");
                         const auto tip=table->model()->data(table->model()->index(0,0),Qt::ToolTipRole).toString();check(tip.contains(QStringLiteral("&lt;img")) && !tip.contains(QStringLiteral("<img")),"Foreign tooltip executes markup");}
            close(inspector);
        }
        check(memory.usage().owners==0 && memory.usage().reservedBytes==0,"Media row evidence retained credits");
        std::cout<<"PASS: "<<checks<<" actual media UI checks; explicit root/replacement, original bytes, missing/duplicate/unsupported, safe labels, reopen/retirement\n";return 0;
    } catch (const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
