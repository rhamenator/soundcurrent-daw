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
#include <QTabWidget>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <array>
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
    check(dialog.snapshot()->report->hasProperties(),"Real inspection has no property metadata");
    auto *properties=find<QTableView>(dialog,"importPropertiesTable");
    await([&]{return properties->model()->rowCount()==7;});
    check(find<QLabel>(dialog,"importInspectionStatus")->text().startsWith("1 original property value"),
          "Summary counted unavailable property entries as found values");
    check(properties->model()->columnCount()==6,"Property/value/unit/status/detail columns missing");
    auto *tabs=find<QTabWidget>(dialog,"importInspectionTabs");
    check(tabs->currentIndex()==0,"Property preview is not the first page");
    check(properties->editTriggers()==QAbstractItemView::NoEditTriggers &&
          !(properties->model()->flags(properties->model()->index(0,0))&Qt::ItemIsEditable),"Property preview became editable");
    bool foundName=false,foundMissing=false;
    for (int row=0;row<properties->model()->rowCount();++row) {
        const auto label=properties->model()->data(properties->model()->index(row,1)).toString();
        const auto value=properties->model()->data(properties->model()->index(row,2)).toString();
        if (label=="Track name") foundName=value=="preserved" && properties->model()->data(properties->model()->index(row,4)).toString()=="Value retained";
        if (label=="Project sample rate") foundMissing=value=="Unavailable" && properties->model()->data(properties->model()->index(row,4)).toString()=="Missing";
    }
    check(foundName && foundMissing,"Original/missing values were collapsed into implicit defaults");
    auto *table=find<QTableView>(dialog,"importInspectionTable");
    await([&]{return table->model()->rowCount()==5;});
    check(table->model()->columnCount()==5,"Original-source columns missing");
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
    auto *save=find<QPushButton>(dialog,"saveImportInspection");
    auto *open=find<QPushButton>(dialog,"openImportInspection");
    check(save->isEnabled() && open->isEnabled(),"Completed inspection cannot be saved/reopened");
    const auto originPid=dialog.snapshot()->childPid;
    const auto destination=input.parent_path()/utf8Path("Inspection été.scinspect");
    check(dialog.saveInspection(destination),"Save request refused");
    await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete && dialog.snapshot()->savedBytes &&
          dialog.snapshot()->savedDurability && !dialog.snapshot()->childPid && !dialog.snapshot()->childExit,
          "Save invented a new child or omitted publication receipt");
    const auto saved=bytes(destination);
    check(dialog.saveInspection(destination),"Collision was not reported asynchronously");
    await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Fault && dialog.snapshot()->report && bytes(destination)==saved,
          "Save collision destroyed source preview or existing bundle");
    const auto relocated=input.parent_path()/utf8Path("移動 inspection.scinspect");
    std::filesystem::rename(destination,relocated);
    const auto movedSource=input.parent_path()/"temporarily-moved-source.rpp";
    std::filesystem::rename(input,movedSource);
    check(dialog.openInspection(relocated),"Relocated bundle not admitted");
    await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete && dialog.snapshot()->report->workerPid()==originPid &&
          !dialog.snapshot()->childPid && !dialog.snapshot()->childExit && !dialog.snapshot()->savedBytes,
          "Reopening requires the original source or invents an active child");
    check(dialog.snapshot()->report->hasProperties() && dialog.snapshot()->report->properties().size()==7,
          "Portable reopen lost original/missing property metadata");
    await([&]{return table->model()->rowCount()==5;});
    check(table->model()->data(table->model()->index(0,3)).toString()=="Unverified",
          "Bundle reopened with a verified property");
    std::filesystem::rename(movedSource,input);
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
void nativeProperties(const std::filesystem::path &root) {
    const auto input=root/utf8Path("Stereo – Ελληνικά.rpp");
    const auto original=bytes(utf8Path(SC_IMPORT_CORPUS_ROOT)/"stereo-gain-pan.rpp"); write(input,original);
    InspectionOptions options; ImportInspectionDialog dialog(nullptr,options); dialog.show();
    check(dialog.inspect(input),"Native source property preview not admitted");
    await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete,"Native source property preview failed");
    auto *table=find<QTableView>(dialog,"importPropertiesTable");
    await([&]{return table->model()->rowCount()==20;});
    bool track=false,item=false,take=false,unicode=false,unsupported=false;
    for (int row=0;row<table->model()->rowCount();++row) {
        const auto label=table->model()->data(table->model()->index(row,1)).toString();
        const auto value=table->model()->data(table->model()->index(row,2)).toString();
        if (label=="Track gain") track=value==QLocale().toString(0.5,'g',17);
        if (label=="Item gain") item=value==QLocale().toString(0.75,'g',17);
        if (label=="Take gain") take=value==QLocale().toString(0.625,'g',17);
        if (label=="Take name") unicode=value==QString::fromUtf8("Prise \"α\" – Запись");
        if (label=="Take rate") unsupported=table->model()->data(table->model()->index(row,4)).toString()=="Unsupported";
    }
    check(track && item && take && unicode && unsupported,"Native values/gain layers/unsupported processing lost in UI");
    if (QCoreApplication::arguments().contains("--screenshots")) {
        QTest::qWait(30); check(dialog.grab().save(".cache/import-property-preview.png"),"Property preview screenshot failed");
    }
    dialog.close(); await([&]{return dialog.retired() && !dialog.isVisible();});
    check(bytes(input)==original,"Native preview changed source bytes");
}
void legacyOutline(const std::filesystem::path &input) {
    InspectionOptions options;
    options.afterChild=[](std::string &encoded,std::size_t) {
        // Qualification-only v1 envelope, preserving exact original nodes/source.
        auto old=nlohmann::json::parse(encoded); old.erase("preview");
        old["protocol"]="sc-import-inspection-v1"; old["adapter"]="rpp-outline-v1"; encoded=old.dump();
    };
    ImportInspectionDialog dialog(nullptr,options); dialog.show();
    check(dialog.inspect(input),"Legacy outline request refused"); await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete && !dialog.snapshot()->report->hasProperties(),"V1 envelope promoted properties");
    auto *tabs=find<QTabWidget>(dialog,"importInspectionTabs");
    await([&]{return tabs->currentIndex()==1;});
    check(!tabs->isTabEnabled(0),"Legacy outline showed invented property metadata");
    const auto destination=input.parent_path()/"legacy-outline.scinspect";
    check(dialog.saveInspection(destination),"Legacy save refused"); await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete,"Legacy save failed");
    check(dialog.openInspection(destination),"Legacy reopen refused"); await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete && !dialog.snapshot()->report->hasProperties() &&
          !dialog.snapshot()->childPid && !dialog.snapshot()->childExit,"Legacy reopen invented values or a live child");
    dialog.close(); await([&]{return dialog.retired() && !dialog.isVisible();});
}
void escapedForeignText(const std::filesystem::path &root) {
    const auto input=root/"literal-markup.rpp";
    write(input,"<REAPER_PROJECT\n<TRACK\nNAME '<b>texte</b>'\n>\n>\n");
    InspectionOptions options; ImportInspectionDialog dialog(nullptr,options); dialog.show();
    check(dialog.inspect(input),"Literal text preview not admitted"); await([&]{return !dialog.snapshot()->busy;});
    check(dialog.snapshot()->phase==InspectionPhase::Complete,"Literal text preview failed");
    auto *table=find<QTableView>(dialog,"importPropertiesTable");
    await([&]{return table->model()->rowCount()==7;});
    bool escaped=false;
    for (int row=0;row<table->model()->rowCount();++row) if (table->model()->data(table->model()->index(row,1)).toString()=="Track name") {
        const auto cell=table->model()->index(row,2);
        const auto tip=table->model()->data(cell,Qt::ToolTipRole).toString();
        escaped=table->model()->data(cell).toString()=="<b>texte</b>" && tip.contains("&lt;b&gt;") && !tip.contains("<b>");
    }
    check(escaped,"Foreign tooltip text was treated as active rich text");
    dialog.close(); await([&]{return dialog.retired() && !dialog.isVisible();});
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
    const auto bundle=root/"window-preview.scinspect";
    check(dialog->saveInspection(bundle),"Window inspection save refused");
    await([&]{return !dialog->snapshot()->busy;});
    check(dialog->snapshot()->phase==InspectionPhase::Complete,"Window inspection save failed");
    check(dialog->openInspection(bundle),"Window inspection reopen refused");
    await([&]{return !dialog->snapshot()->busy;});
    check(dialog->snapshot()->phase==InspectionPhase::Complete && !dialog->snapshot()->childPid,
          "Window inspection reopen failed or invented an active child");
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
void valueSummary(const std::filesystem::path &root) {
    ImportInspectionDialog dialog(nullptr,{}); dialog.show();
    auto *table=find<QTableView>(dialog,"importPropertiesTable");
    auto *summary=find<QLabel>(dialog,"importInspectionStatus");
    const auto input=root/"summary.rpp";
    const std::array<std::pair<std::string,int>,3> cases{{
        {"<REAPER_PROJECT 0.1 7.82\n>\n",0},
        {"<REAPER_PROJECT 0.1 7.82\n <TRACK\n NCHAN nan\n >\n>\n",0},
        {"<REAPER_PROJECT 0.1 7.82\n <TRACK\n NCHAN 2\n >\n>\n",1}}};
    for (const auto &[source,count]:cases) {
        write(input,source); check(dialog.inspect(input),"Summary fixture was not admitted");
        await([&]{return !dialog.snapshot()->busy;});
        check(dialog.snapshot()->phase==InspectionPhase::Complete,"Summary fixture failed inspection");
        await([&]{return table->model()->rowCount()==
            int(dialog.snapshot()->report->properties().size());});
        const auto expected=QString::number(count)+" original property value";
        await([&]{return summary->text().startsWith(expected);});
        check(summary->text().startsWith(expected),
              "Missing or invalid values inflated the summary count");
    }
    dialog.close(); await([&]{return dialog.retired();});
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir temporary; check(temporary.isValid(),"Temporary root failed");
        const auto root=utf8Path(temporary.path().toUtf8().toStdString());
        const auto input=root/utf8Path("Séance Ελληνικά.rpp");
        const std::string original="<REAPER_PROJECT 0.1 7.74\n <TRACK foreign\n NAME preserved\n >\n>\n";
        write(input,original);
        directDialog(input); nativeProperties(root); legacyOutline(input); escapedForeignText(root);
        valueSummary(root); busyClose(input); window(root/"session",input);
        check(bytes(input)==original,"Inspection modified original project bytes");
        std::cout<<"PASS: "<<checks<<" import preview UI checks; actual child, no project mutation, async retirement.\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
