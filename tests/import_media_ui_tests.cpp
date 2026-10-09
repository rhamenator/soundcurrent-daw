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
#include <QScreen>
#include <QLabel>
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
std::string read(const std::filesystem::path &p) {std::ifstream in(p,std::ios::binary);check(bool(in),"Cannot read owned media");return {std::istreambuf_iterator<char>(in),{}};}
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
            check(media->width()<=media->screen()->availableGeometry().width() &&
                  media->height()<=media->screen()->availableGeometry().height(),"Media checklist exceeds display");
            auto *table=media->findChild<QTableView *>(QStringLiteral("importMediaTable"));
            check(table && table->model()->rowCount()==1,"Source-file media inventory wrong");
            check(!media->snapshot()->childPid && !media->snapshot()->selection,"Opening checklist accessed media automatically");
            check(!media->copySnapshot()->childPid && !media->copyChecked(0,root),"Opening checklist or unchecked row copied media");
            check(!inspector.inspect(file),"A different source was admitted under a live media dialog");
            table->selectRow(0);unsigned pulses=0;QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++pulses;});heartbeat.start();
            check(media->checkReference(0,corpus),"Explicit media folder/reference not admitted");wait([&]{return !media->snapshot()->busy;});heartbeat.stop();
            check(pulses>0,"GUI stopped servicing events during media work");
            check(media->snapshot()->phase==WaveCheckPhase::Complete && media->snapshot()->report->audio().frames==96000,"Actual corpus media not validated");
            auto *copy=media->findChild<QPushButton *>(QStringLiteral("copyCheckedMedia"));
            wait([&]{return copy && copy->isEnabled();});
            const auto destination=root/"owned-copies";std::filesystem::create_directory(destination);
            pulses=0;heartbeat.start();check(media->copyChecked(0,destination),"Desktop checked copy refused");
            wait([&]{return !media->copySnapshot()->busy;});heartbeat.stop();const auto copied=media->copySnapshot();
            check(pulses>0 && copied->phase==MediaCopyPhase::Committed && copied->childPid && copied->childExit==0,"Actual desktop copy blocked events or lost commit");
            wait([&]{return table->model()->data(table->model()->index(0,5)).toString()==QStringLiteral("Verified copy");});
            check(media->width()<=media->screen()->availableGeometry().width() && media->height()<=media->screen()->availableGeometry().height(),"Copy status exceeds display");
            const auto operation=destination/copied->selection->operation.str();
            check(read(operation/"media.wav")==read(corpus/"media/mono.wav") && copied->provenance->data().originalReference=="media/mono.wav","Desktop copied bytes/original provenance differ");
            check(media->recoverCopied(operation),"Explicit desktop recovery refused");wait([&]{return !media->copySnapshot()->busy;});
            check(media->copySnapshot()->phase==MediaCopyPhase::Committed && !media->copySnapshot()->durability,"Desktop recovery failed or invented historical durability");
            check(!std::filesystem::exists(destination/"project.json") && inspector.snapshot()->report->source()==original,"Desktop copy converted or changed project");
            check(table->model()->data(table->model()->index(0,0)).toString()==QStringLiteral("media/mono.wav"),"Original media reference rewritten");
            check(media->checkReference(0,root),"Missing-media diagnostic refused");wait([&]{return !media->snapshot()->busy;});
            check(media->snapshot()->error==ErrorCode::MissingMedia,"Missing-media status lost");
            wait([&]{return table->model()->data(table->model()->index(0,5)).toString()==QStringLiteral("Not copied");});
            check(media->checkReplacement(0,corpus/"media/stereo.wav"),"Explicit replacement refused");wait([&]{return !media->snapshot()->busy;});
            check(media->snapshot()->phase==WaveCheckPhase::Complete && media->snapshot()->report->audio().channels==2 && media->snapshot()->selection->selectedFilename,"Selected replacement not checked");
            check(table->model()->data(table->model()->index(0,0)).toString()==QStringLiteral("media/mono.wav"),"Replacement altered original token");
            auto *clear=media->findChild<QPushButton *>(QStringLiteral("clearMediaChecks"));
            wait([&]{return clear->isEnabled() && table->model()->data(table->model()->index(0,2)).toString()==QStringLiteral("Checked snapshot");});
            check(table->model()->data(table->model()->index(0,3)).toString().contains(QStringLiteral("2 channels")),"Channel format was not displayed");
            check(!table->model()->data(table->model()->index(0,4)).toString().isEmpty(),"Sample peak was not displayed");
            wait([&]{return copy->isEnabled();});
            if (argc>1) check(media->grab().save(QString::fromLocal8Bit(argv[1])),"Media screenshot save failed");
            clear->click();
            check(!media->snapshot()->report && !media->snapshot()->selection,"Clearing local choices retained active report");
            check(!media->copySnapshot()->selection && std::filesystem::exists(operation/"media.wav"),"Clearing choices retained local copy request or removed committed files");
            const auto bundle=root/"saved.scinspect";check(inspector.saveInspection(bundle),"Source-only inspection save refused");wait([&]{return !inspector.snapshot()->busy;});
            check(inspector.snapshot()->phase==InspectionPhase::Complete,"Inspection save failed");
            media->close();wait([&]{return media->retired();});
            check(inspector.openInspection(bundle),"Saved inspection reopen refused");wait([&]{return !inspector.snapshot()->busy;});
            auto *reopened=inspector.openMediaCheck();check(reopened && !reopened->snapshot()->selection && !reopened->snapshot()->childPid,"Reopening granted historical media access");
            reopened->findChild<QTableView *>(QStringLiteral("importMediaTable"))->selectRow(0);
            check(reopened->recoverCopied(operation),"Source-only reopen cannot explicitly recover owned copy");wait([&]{return !reopened->copySnapshot()->busy;});
            check(reopened->copySnapshot()->phase==MediaCopyPhase::Committed,"Reopened desktop copy recovery failed");
            const auto ownedSource=root/"owned-source.wav";const auto sample=read(corpus/"media/mono.wav");write(ownedSource,sample);
            check(reopened->checkReplacement(0,ownedSource),"Owned replacement not checked");wait([&]{return !reopened->snapshot()->busy;});
            auto *copyAgain=reopened->findChild<QPushButton *>(QStringLiteral("copyCheckedMedia"));wait([&]{return copyAgain->isEnabled();});
            auto changed=sample;changed.back()^=1;write(ownedSource,changed);check(reopened->copyChecked(0,destination),"Changed-source copy request not admitted");
            wait([&]{return !reopened->copySnapshot()->busy;});
            check(reopened->copySnapshot()->phase==MediaCopyPhase::RecoveryRequired,"Changed checked source was adopted or publication absence invented");
            auto *recoverLast=reopened->findChild<QPushButton *>(QStringLiteral("recoverCopiedMedia"));
            wait([&]{return recoverLast->text()==QStringLiteral("Check last copy outcome") && recoverLast->isEnabled();});recoverLast->click();
            wait([&]{return !reopened->copySnapshot()->busy;});check(reopened->copySnapshot()->phase==MediaCopyPhase::NoEvidence,"Missing operation folder cannot be explicitly checked without a file picker");
            write(ownedSource,sample);
            {
                MediaCopyOptions slowCopy;slowCopy.deadlineMilliseconds=5000;slowCopy.childMemoryBytes=63*1024*1024+2;
                slowCopy.program=QCoreApplication::applicationDirPath()+
#ifdef _WIN32
                    QStringLiteral("/sc-media-copy-lifecycle-probe.exe");
#else
                    QStringLiteral("/sc-media-copy-lifecycle-probe");
#endif
                WaveCheckOptions checkOptions;checkOptions.memory=memory;checkOptions.maximumSourceBytes=999992;
                ImportMediaDialog liveCopy(nullptr,inspector.snapshot()->report,checkOptions,slowCopy);liveCopy.show();
                liveCopy.findChild<QTableView *>(QStringLiteral("importMediaTable"))->selectRow(0);
                check(liveCopy.checkReplacement(0,ownedSource),"Live copy source not checked");wait([&]{return !liveCopy.snapshot()->busy;});
                auto *button=liveCopy.findChild<QPushButton *>(QStringLiteral("copyCheckedMedia"));wait([&]{return button->isEnabled();});
                check(liveCopy.copyChecked(0,destination),"Live copy not admitted");const auto id=liveCopy.copySnapshot()->selection->operation;
                wait([&]{return liveCopy.copySnapshot()->childPid && std::filesystem::exists(destination/(id.str()+".ready"));});
                QElapsedTimer closeTime;closeTime.start();liveCopy.close();check(closeTime.elapsed()<100 && liveCopy.isVisible(),"Live-copy close blocked or retired a running child");
                wait([&]{return liveCopy.retired() && !liveCopy.isVisible();});
                check(liveCopy.copySnapshot()->childExit.has_value() && liveCopy.copySnapshot()->phase==MediaCopyPhase::RecoveryRequired,"Closing UI discarded uncertain committed outcome");
                check(std::filesystem::exists(destination/id.str()/"receipt.json"),"Test did not terminate after actual publication");
                check(reopened->recoverCopied(destination/id.str()),"Post-close published copy cannot recover");wait([&]{return !reopened->copySnapshot()->busy;});
                check(reopened->copySnapshot()->phase==MediaCopyPhase::Committed,"Post-close explicit recovery lost committed media");
            }
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
    } catch (const std::exception &e) {std::cerr<<"Media UI failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
