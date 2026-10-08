// SPDX-License-Identifier: GPL-3.0-only
#include "localization.hpp"
#include "studio_window.hpp"
#include "timeline_view.hpp"
#include "session_list_model.hpp"
#include "equipment_profiles.hpp"
#include "fake_playback_endpoint.hpp"
#include "fake_recording_endpoint.hpp"
#include <soundcurrent/export.hpp>
#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <sndfile.h>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace soundcurrent::daw;
using namespace soundcurrent::daw::ui;
namespace locale = soundcurrent::daw::i18n;
namespace {
int checks = 0;
void check(bool value, const char *why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class Predicate> void await(Predicate p) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!p()) { check(std::chrono::steady_clock::now() < end, "Localized UI workflow timed out"); QTest::qWait(2); }
}
QString pathText(const std::filesystem::path &p) {
#ifdef _WIN32
    return QString::fromStdWString(p.wstring());
#else
    return QString::fromUtf8(p.string());
#endif
}
QByteArray bytes(const std::filesystem::path &p) {
    QFile f(pathText(p));
    check(f.open(QIODevice::ReadOnly), "Owned fixture unreadable"); return f.readAll();
}
Session project(const std::filesystem::path &root) {
    std::filesystem::create_directories(root / "media");
    std::filesystem::create_directories(root / "exports");
    auto s = makeOneTrackSession("Séance Δοκιμή — العربية", "Own track — Ελληνικά");
    Asset asset; asset.relativePath = "media/Δοκιμή.wav"; asset.frames = 128;
    const auto file = root / utf8Path(asset.relativePath);
    SF_INFO info{}; info.samplerate = 48000; info.channels = 1; info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
#ifdef _WIN32
    auto *wave = sf_wchar_open(file.c_str(), SFM_WRITE, &info);
#else
    auto *wave = sf_open(file.c_str(), SFM_WRITE, &info);
#endif
    check(wave, "Owned Unicode WAV creation failed");
    std::array<float,128> samples{}; samples.fill(.25f);
    check(sf_writef_float(wave,samples.data(),samples.size())==sf_count_t(samples.size()),"Owned WAV write failed");
    check(sf_close(wave)==0,"Owned WAV close failed");
    asset.sha256=QCryptographicHash::hash(bytes(file),QCryptographicHash::Sha256).toHex().toStdString();
    s.assets={asset}; Clip clip;clip.assetId=asset.id;clip.lengthFrames=128;s.tracks[0].clips={clip};s.exportEndFrame=128;
    ProjectStore(root).save(s);return s;
}
void runtimeAndPreferences() {
    check(locale::languages().size()==33,"Draft catalogs missing");
    check(locale::resolve("de_DE")=="de" && locale::resolve("fr-CA")=="fr","Explicit regional fallback failed");
    check(locale::resolve("pt-BR")=="pt-BR" && locale::resolve("pt-PT")=="pt-PT","Distinct regional catalogs collapsed");
    check(locale::resolve("zh-Hant-TW")=="zh-Hant" && locale::resolve("zh-Hans-CN")=="zh-Hans","Script catalog collapsed");
    check(locale::resolve("sr-Latn")=="en" && locale::resolve("de-Latn-DE")=="en","An unapproved script was guessed");
    for(const auto &bad:QStringList{"../bad","../../de","de/../fr","",QString(100,'a')})
        check(locale::resolve(bad)=="en","Unsafe or unavailable language did not fall back");
    check(locale::chooseLanguage("fr","de","he",{"en"})=="fr","CLI precedence failed");
    check(locale::chooseLanguage({},"de","he",{"en"})=="de","Preference precedence failed");
    check(locale::chooseLanguage({},"system","he",{"en"})=="he","Environment precedence failed");
    check(locale::chooseLanguage({},"system",{}, {"sr-Latn","fr-CA"})=="fr-CA","System UI choices failed");
    for(const auto &l:locale::languages()) {
        locale::Runtime runtime(l.tag,"en-US");
        check(runtime.loaded()==l.tag && runtime.catalogLoaded(),"Embedded catalog failed to install");
        check(l.translated>0 && l.translated<=l.total,"Empty or inflated language coverage");
        if(l.tag!="en") check(QCoreApplication::translate("Localization","Interface language")!="Interface language","Catalog keys do not match runtime context");
    }
    {
        locale::Runtime runtime("de","de-DE");
        check(StudioWindow::tr("Playback")==QString::fromUtf8("Wiedergabe"),"Actual DAW context failed");
        check(QCoreApplication::translate("EquipmentProfiles","Undo")==QString::fromUtf8("Rückgängig"),"Equipment context failed");
        check(StudioWindow::tr("Untranslated probe")=="Untranslated probe","English fallback failed");
        locale::savePreferences({"de","fr-CA"});
        std::unique_ptr<QDialog> dialog(locale::settingsDialog());
        auto *language=dialog->findChild<QComboBox *>("uiLanguage");auto *format=dialog->findChild<QComboBox *>("formatLocale");
        check(language && format && format->findData("de-DE")>=0 && format->findData("fr-CA")>=0,"Independent locale selectors missing");
        language->setCurrentIndex(language->findData("fr"));format->setCurrentIndex(format->findData("de-DE"));
        check(locale::loadPreferences().language=="de","Unsaved dialog persisted changes");
        auto *buttons=dialog->findChild<QDialogButtonBox *>();buttons->button(QDialogButtonBox::Save)->click();
        const auto saved=locale::loadPreferences();check(saved.language=="fr" && saved.formatLocale=="de-DE","Saved language/format preference lost");
        check(runtime.loaded()=="de" && QLocale().name().startsWith("de"),"Preference editing changed live runtime");
    }
    {
        locale::Runtime runtime("qps-rtl","en-US");
        auto text=QCoreApplication::translate("Probe","<b>Value %L1 / %2 && %n</b>",nullptr,2);
        check(text.contains("%L1") && text.contains("%2") && text.contains("&&") && text.contains("<b>") && !text.contains("%n"),"Pseudo translator damaged placeholders, markup or numerus replacement");
        check(QApplication::layoutDirection()==Qt::RightToLeft,"Pseudo RTL direction failed");
    }
}
void uiWorkflow(const std::filesystem::path &root, const QString &language, const Session &initial, bool edit) {
    locale::Runtime runtime(language,"de-DE");
    auto playback=std::make_shared<playback_fixture::Counters>();auto recording=std::make_shared<recording_fixture::Counters>();
    StudioWindow window(nullptr,playback_fixture::options(playback),recording_fixture::options(recording));
    window.resize(1000,640);window.show();window.openProject(root);
    await([&]{return window.snapshot()->session && window.snapshot()->io==IoOperation::None && window.findChild<QDoubleSpinBox *>("gain_db0");});
    check(*window.snapshot()->session==initial,"UI language changed canonical project or IDs");
    auto *timeline=dynamic_cast<TimelineView *>(window.findChild<QWidget *>("audioTimeline"));
    check(timeline && timeline->layoutDirection()==Qt::LeftToRight,"RTL reversed musical timeline layout");
    auto *action=window.findChild<QAction *>("languageSettingsAction");check(action,"Language menu missing");action->trigger();
    auto *settings=window.findChild<QDialog *>("languageSettingsDialog");check(settings,"Real window did not open language settings");settings->reject();QTest::qWait(1);
    auto *gain=window.findChild<QDoubleSpinBox *>("gain_db0");check(gain->text().contains(','),"Real EQ numeric control ignores format locale");
    if(edit) {
        gain->findChild<QLineEdit *>()->setText("-12,50");gain->interpretText();
        await([&]{return window.snapshot()->session->tracks[0].eq.bands[0].gainDb==-12.5;});
        auto *save=window.findChild<QAction *>("saveAction");check(save,"Save action missing");save->trigger();
        await([&]{return !window.snapshot()->dirty && window.snapshot()->io==IoOperation::None;});
        check(ProjectStore(root).load()==*window.snapshot()->session,"Localized edit Save/reopen changed state");
    }
    const auto before=bytes(root/"project.json");
    check(window.grab().save(pathText(root/(language.toStdString()+".png"))),
          "Localized Unicode screenshot path failed");
    window.close();await([&]{return window.snapshot()->closed;});
    check(bytes(root/"project.json")==before,"Language settings or Close changed saved project bytes");
    check(playback->activated==0 && recording->activated==0,"Localization fixture activated audio");
}
QImage profileChart(const QString &language) {
    locale::Runtime runtime(language,"en-US");
    equipment::Profile p;p.id="locale-test-source";p.kind="microphone";p.brand="Été Ελληνικά";p.model="Synthetic owned profile";
    p.filters.append({1000,-2,1});p.response={{20,0},{1000,2},{20000,0}};
    const auto original=QJsonDocument(equipment::serialize(p)).toJson();QImage image;std::exception_ptr error;
    QTimer::singleShot(0,[&]{
        auto *editor=qobject_cast<QDialog *>(QApplication::activeModalWidget());
        try {
            check(editor,"Modal equipment editor missing");auto *plot=editor->findChild<QWidget *>("equipmentCurve");
            check(plot && plot->layoutDirection()==Qt::LeftToRight,"RTL reversed numerical frequency chart");
            plot->setFixedSize(640,180);image=plot->grab().toImage();
        }catch(...){error=std::current_exception();}
        if(editor) {
            QTimer::singleShot(0,[&,editor]{
                try {
                    auto *prompt=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                    check(prompt && prompt->button(QMessageBox::Discard),"Profile discard prompt missing");
                    prompt->button(QMessageBox::Discard)->click();
                }catch(...){error=std::current_exception();editor->QDialog::reject();}
            });
            editor->reject();
        }
    });
    equipment::saveNewProfile(nullptr,p);if(error)std::rethrow_exception(error);
    check(QJsonDocument(equipment::serialize(p)).toJson()==original,"Chart or localized editor changed profile IDs/data");return image;
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setOrganizationName("SoundCurrentDAWTest");app.setApplicationName("Localization");
    const auto root=std::filesystem::temp_directory_path()/("sc-localization-"+Id::generate().str());std::filesystem::create_directories(root);
    std::cout<<"Owned localization fixture root: "<<pathText(root).toUtf8().toStdString()<<'\n';
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,pathText(root));
    try {
        const auto previous=QLocale();const auto direction=QApplication::layoutDirection();runtimeAndPreferences();
        check(QLocale()==previous && QApplication::layoutDirection()==direction,"Runtime lifetime did not restore locale/direction");
        const auto projectRoot=root/utf8Path("Séance Ελληνικά");auto original=project(projectRoot);
        uiWorkflow(projectRoot,"de",original,true);auto edited=ProjectStore(projectRoot).load();
        uiWorkflow(projectRoot,"fr",edited,false);uiWorkflow(projectRoot,"qps-rtl",edited,false);uiWorkflow(projectRoot,"qps-ploc",edited,false);
        ExportSpec spec(edited.tracks[0].id);spec.endFrame=128;spec.blockFrames=16;
        std::string hash;
        for(const auto &language:QStringList{"en","de","he","qps-rtl"}) {
            locale::Runtime runtime(language,"de-DE");auto result=exportTrackWav(projectRoot,edited,projectRoot/"exports"/(language.toStdString()+".wav"),spec);
            check(result.frames==128,"Localized export frame count changed");
            if(hash.empty())hash=result.sampleSha256;else check(hash==result.sampleSha256,"UI/format locale changed rendered audio samples");
        }
        check(profileChart("en")==profileChart("qps-rtl"),"Frequency chart pixels changed under RTL");
        std::cout<<"PASS: "<<checks<<" checks; 33 draft catalog loads, real DAW contexts, decimal edit/Save/reopen, settings, stable render samples and RTL charts. No native audio; no language fully qualified.\n";return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
