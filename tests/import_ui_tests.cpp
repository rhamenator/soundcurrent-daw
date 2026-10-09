// SPDX-License-Identifier: GPL-3.0-only
#include "studio_window.hpp"
#include "import_inspection_dialog.hpp"
#include "export_fixture.hpp"
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QTableView>
#include <QTemporaryDir>
#include <iostream>
using namespace export_fixture;
using namespace soundcurrent::daw::ui;
namespace {
template<class T> T *find(QWidget &w,const char *name) {
    auto *p=w.findChild<T *>(name); check(p!=nullptr,"Missing import UI control"); return p;
}
void directDialog(const std::filesystem::path &input) {
    ResourceLedger memory(128*1024*1024);
    InspectionOptions options; options.memory=memory;
    ImportInspectionDialog dialog(nullptr,options);
    dialog.show();
    check(dialog.inspect(input),"Dialog did not admit first inspection");
    await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete,"Dialog real worker failed");
    auto *table=find<QTableView>(dialog,"importInspectionTable");
    await([&]{return table->model()->rowCount()==5;});
    check(table->model()->columnCount()==4,"Preview columns changed");
    check(table->model()->data(table->model()->index(0,0)).toString()=="REAPER_PROJECT",
          "Raw source key missing");
    check(table->model()->data(table->model()->index(0,3)).toString()=="Unverified",
          "Inspection promoted semantic compatibility");
    check(!(table->model()->flags(table->model()->index(0,0))&Qt::ItemIsEditable),
          "Source inspection became editable");
    check(!table->model()->setData(table->model()->index(0,0),"overwrite"),
          "Read-only model accepted mutation");
    check(table->editTriggers()==QAbstractItemView::NoEditTriggers,"Table enabled editing");
    check(dialog.width()<=dialog.screen()->availableGeometry().width() &&
          dialog.height()<=dialog.screen()->availableGeometry().height(),"Dialog exceeds display");
    for (auto *button:dialog.findChildren<QPushButton *>())
        check(button->text()!="Apply","Unverified inspection exposed Apply");
    const auto originalLocale=QLocale();
    QLocale::setDefault(QLocale(QLocale::Arabic,QLocale::Egypt));
    dialog.setLayoutDirection(Qt::RightToLeft);
    QEvent changed(QEvent::LanguageChange); QApplication::sendEvent(&dialog,&changed);
    check(table->model()->data(table->model()->index(0,2)).toString()==QLocale().toString(qulonglong(1)),
          "Preview line numbers ignored locale");
    check(dialog.layoutDirection()==Qt::RightToLeft,"Preview lost RTL layout");
    QLocale::setDefault(originalLocale); dialog.setLayoutDirection(Qt::LeftToRight);
    QApplication::sendEvent(&dialog,&changed);
    if (QCoreApplication::arguments().contains("--screenshots")) {
        QTest::qWait(30);
        check(dialog.grab().save(".cache/import-inspection-dialog.png"),"Screenshot failed");
    }
    dialog.close(); await([&]{return dialog.retired() && !dialog.isVisible();});
    await([&]{return memory.usage().reservedBytes==0;});
}
void busyClose(const std::filesystem::path &input) {
    Gate gate;
    ResourceLedger memory(128*1024*1024);
    InspectionOptions options; options.memory=memory;
    options.beforeSpawn=[&]{gate.wait();};
    ImportInspectionDialog dialog(nullptr,options); Release release{gate}; dialog.show();
    check(dialog.inspect(input),"Busy-close fixture refused");
    await([&]{return gate.entered.load();});
    dialog.close();
    check(dialog.isVisible() && !dialog.retired(),"Dialog closed before worker retirement");
    check(!dialog.inspect(input),"Closing dialog accepted work");
    gate.released=true;
    await([&]{return dialog.retired() && !dialog.isVisible();});
    check(memory.usage().reservedBytes==0,"Canceled dialog retained a bank");
}
void window(const std::filesystem::path &root,const std::filesystem::path &input) {
    std::filesystem::create_directory(root);
    ProjectStore(root).save(makeOneTrackSession("Existing session","Original track"));
    StudioWindow w; w.show(); w.openProject(root);
    await([&]{return w.snapshot()->session && w.snapshot()->io==IoOperation::None;});
    const auto before=w.snapshot();
    auto *action=find<QAction>(w,"inspectForeignProjectAction"); action->trigger();
    auto *dialog=dynamic_cast<ImportInspectionDialog *>(find<QDialog>(w,"importInspectionDialog"));
    check(dialog!=nullptr,"Import dialog type missing");
    check(dialog->isVisible(),"File menu did not show import preview");
    check(w.inspectForeignProject(input),"Window inspection refused");
    await([&]{auto v=w.importInspectionSnapshot();return v && !v->busy;});
    check(w.importInspectionSnapshot()->phase==InspectionPhase::Complete,"Window real child failed");
    const auto after=w.snapshot();
    check(after->session==before->session && after->projectEpoch==before->projectEpoch &&
          after->modelRevision==before->modelRevision && after->savedRevision==before->savedRevision &&
          after->completedCommands==before->completedCommands && !after->dirty,
          "Inspection altered canonical project or command history");
    w.close(); await([&]{return !w.isVisible();});
    check(w.snapshot()->closed,"Window closed before project retirement");
    check(!w.inspectForeignProject(input),"Closed window admitted inspection");
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir temporary; check(temporary.isValid(),"Temporary root failed");
        const auto root=utf8Path(temporary.path().toUtf8().toStdString());
        const auto input=root/utf8Path("Séance Ελληνικά.rpp");
        const std::string original="<REAPER_PROJECT 0.1 7.74\n <TRACK foreign\n NAME preserved\n >\n>\n";
        write(input,original);
        directDialog(input); busyClose(input); window(root/"session",input);
        check(bytes(input)==original,"Inspection modified original project bytes");
        std::cout<<"PASS: "<<checks<<" import preview UI checks; actual child, no project mutation, async retirement.\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
