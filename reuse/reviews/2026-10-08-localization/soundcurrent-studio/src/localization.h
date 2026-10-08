// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "localization_text.h"
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QListView>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>
#include <algorithm>
#include <memory>
namespace soundcurrent::i18n {
struct Language { QString tag, nativeName; int translated, total; };
inline QVector<Language> languages() {
    QFile file(":/i18n/catalogs.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    QVector<Language> result;
    for (const auto &v : QJsonDocument::fromJson(file.readAll()).array()) {
        auto o=v.toObject();result.append({o["tag"].toString(),o["name"].toString(),o["translated"].toInt(),o["total"].toInt()});
    }
    return result;
}
inline QString normalize(QString tag) { return tag.trimmed().replace('_','-'); }
inline QString resolve(const QString &requested) {
    auto tag=normalize(requested);
    if(tag=="qps-ploc" || tag=="qps-rtl") return tag;
    const auto supported=languages();
    for(const auto &language:supported)
        if(language.tag.compare(tag,Qt::CaseInsensitive)==0) return language.tag;
    const QRegularExpression syntax("^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{1,8})*$");
    if(!syntax.match(tag).hasMatch())return "en";
    auto parts=tag.split('-');
    // Unicode/private-use extensions affect formatting, not catalog selection.
    for(int i=1;i<parts.size();++i)if(parts[i].size()==1){parts=parts.mid(0,i);break;}
    tag=parts.join('-');
    const QLocale requestedLocale(tag);
    const auto languageCode=QLocale::languageToCode(requestedLocale.language());
    if(languageCode.compare(parts.front(),Qt::CaseInsensitive)!=0)return "en";
    int regionIndex=1;
    if(parts.size()>1 && parts[1].size()==4){
        if(QLocale::scriptToCode(requestedLocale.script()).compare(parts[1],Qt::CaseInsensitive)!=0)return "en";
        regionIndex=2;
    }
    QString explicitRegion;
    if(parts.size()>regionIndex && (parts[regionIndex].size()==2 ||
       QRegularExpression("^[0-9]{3}$").match(parts[regionIndex]).hasMatch()))explicitRegion=parts[regionIndex];
    const auto desiredRegion=explicitRegion.isEmpty()?QLocale::territoryToCode(requestedLocale.territory()):explicitRegion;
    QString regionalMatch;
    for(const auto &language:supported){
        if(language.tag.section('-',0,0).compare(languageCode,Qt::CaseInsensitive)!=0)continue;
        const QLocale candidate(language.tag);
        if(candidate.script()!=requestedLocale.script())continue;
        const auto candidateParts=language.tag.split('-');
        const bool regionSpecific=candidateParts.size()>1 && (candidateParts.last().size()==2 || candidateParts.last().size()==3);
        // Generic language/script packs cover their regions, retaining script identity.
        if(!regionSpecific)return language.tag;
        if(candidateParts.last().compare(desiredRegion,Qt::CaseInsensitive)==0)regionalMatch=language.tag;
    }
    return regionalMatch.isEmpty()?QString("en"):regionalMatch;
}
inline QString selectedLanguage() {
    const auto args=QCoreApplication::arguments();
    const auto i=args.indexOf("--language");
    if(i>=0 && i+1<args.size()) return args[i+1];
    const auto saved=QSettings().value("i18n/language","system").toString();
    if(saved!="system") return saved;
    const auto env=qEnvironmentVariable("SOUNDCURRENT_LANGUAGE");
    if(!env.isEmpty()) return env;
    const auto choices=QLocale::system().uiLanguages();
    for(const auto &choice:choices) if(resolve(choice)!="en" || choice.startsWith("en")) return choice;
    return "en";
}
class PseudoTranslator final : public QTranslator {
public:
    explicit PseudoTranslator(bool rtl):rtl_(rtl) {}
    bool isEmpty() const override { return false; }
    QString translate(const char *,const char *source,const char *,int) const override {
        QString input=QString::fromUtf8(source),out;
        const QRegularExpression protectedParts("(<[^>]*>|%L?[0-9]+|%n|&&|&)");
        auto append=[&](const QString &s){
            const QString plain="aeiouAEIOU", accented=QString::fromUtf8("áëïöüÁËÏÖÜ");
            for(const auto c:s) {const auto i=plain.indexOf(c);out+=i<0?c:accented[i];if(c.isLetter())out+=c;}
        };
        auto matches=protectedParts.globalMatch(input);qsizetype pos=0;
        while(matches.hasNext()){auto m=matches.next();append(input.mid(pos,m.capturedStart()-pos));out+=m.captured();pos=m.capturedEnd();}
        append(input.mid(pos));
        return (rtl_?QString::fromUtf8("\u2067[אב "):QString("["))+out+(rtl_?QString::fromUtf8("]\u2069"):QString("]"));
    }
private: bool rtl_;
};
// Translate Qt's standard action captions through the same embedded app catalog.
// Native operating-system dialogs keep the operating system language.
class StandardActionTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override {
        const QByteArray name(context);
        if (name != "QPlatformTheme" && name != "QDialogButtonBox") return {};
        const QByteArray action(source);
        if (action == "OK") return text("OK");
        if (action == "&Yes") return text("Yes");
        if (action == "&No") return text("No");
        if (action == "Yes to &All") return text("Yes to All");
        if (action == "N&o to All") return text("No to All");
        if (action == "Open") return text("Open");
        if (action == "Save") return text("Save");
        if (action == "Save All") return text("Save All");
        if (action == "Close") return text("Close");
        if (action == "Cancel") return text("Cancel");
        if (action == "Discard") return text("Discard");
        if (action == "Apply") return text("Apply");
        if (action == "Reset") return text("Reset");
        if (action == "Restore Defaults") return text("Restore Defaults");
        if (action == "Retry") return text("Retry");
        if (action == "Abort") return text("Abort");
        if (action == "Ignore") return text("Ignore");
        if (action == "Help") return text("Help");
        return {};
    }
};
class Runtime {
public:
    void initialize(bool englishTest=false) {
        requested_=englishTest?"en":selectedLanguage();loaded_=resolve(requested_);
        const auto format=englishTest?QString("en-US"):QSettings().value("i18n/formatLocale","system").toString();
        const auto locale=format=="system"?QLocale::system():QLocale(format);
        QLocale::setDefault(locale);
        if(loaded_.startsWith("qps-")) translator_=std::make_unique<PseudoTranslator>(loaded_=="qps-rtl");
        else if(loaded_!="en") {
            auto t=std::make_unique<QTranslator>();
            if(t->load(":/i18n/soundcurrent_"+loaded_+".qm"))translator_=std::move(t);
            else loaded_="en";
        }
        QCoreApplication::installTranslator(&standardActions_);
        if(translator_)QCoreApplication::installTranslator(translator_.get());
        const auto direction=loaded_=="qps-rtl"?Qt::RightToLeft:QLocale(loaded_).textDirection();
        QApplication::setLayoutDirection(direction);
    }
    ~Runtime(){if(translator_)QCoreApplication::removeTranslator(translator_.get());QCoreApplication::removeTranslator(&standardActions_);}
    QString requested() const{return requested_;} QString loaded() const{return loaded_;}
private: QString requested_,loaded_;StandardActionTranslator standardActions_;std::unique_ptr<QTranslator> translator_;
};
inline QGroupBox *settingsPanel() {
    auto *box=new QGroupBox(text("Language and regional settings"));
    auto *form=new QFormLayout(box);auto *language=new QComboBox;language->setObjectName("uiLanguage");
    language->setAccessibleName(text("Interface language"));
    language->addItem(text("Use system language"),"system");language->addItem("English (en)","en");
    for(const auto &l:languages()) if(l.tag!="en")language->addItem(l.nativeName+" ("+l.tag+")",l.tag);
    language->addItem(text("Expanded test language"),"qps-ploc");language->addItem(text("Right-to-left test language"),"qps-rtl");
    auto saved=QSettings().value("i18n/language","system").toString();
    auto index=language->findData(saved);language->setCurrentIndex(index>=0?index:0);
    form->addRow(text("Interface language"),language);
    auto *format=new QComboBox;
    format->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    format->setMinimumContentsLength(24);format->setMaxVisibleItems(12);
    auto *view=new QListView;view->setUniformItemSizes(true);format->setView(view);
    format->setObjectName("formatLocale");format->setAccessibleName(text("Number and date format"));
    format->addItem(text("Use system locale"),"system");
    auto locales=QLocale::matchingLocales(QLocale::AnyLanguage,QLocale::AnyScript,QLocale::AnyTerritory);
    std::sort(locales.begin(),locales.end(),[](const QLocale&a,const QLocale&b){return a.name()<b.name();});
    QStringList seen;
    for(const auto &locale:locales){
        if(locale.language()==QLocale::C)continue;
        auto tag=locale.bcp47Name();const auto region=QLocale::territoryToCode(locale.territory());
        if(!region.isEmpty() && !tag.endsWith("-"+region))tag+="-"+region;
        if(seen.contains(tag))continue;
        seen.append(tag);
        format->addItem(locale.nativeLanguageName()+" — "+locale.nativeTerritoryName()+" ("+tag+")",tag);
    }
    index=format->findData(QSettings().value("i18n/formatLocale","system").toString());format->setCurrentIndex(index>=0?index:0);
    form->addRow(text("Number and date format"),format);
    auto *help=new QLabel;help->setWordWrap(true);help->setTextFormat(Qt::PlainText);form->addRow(help);
    auto update=[language,help]{
        auto requested=language->currentData().toString();if(requested=="system")requested=selectedLanguage();
        auto tag=resolve(requested);int done=0,total=0;
        for(const auto &l:languages())if(l.tag==tag){done=l.translated;total=l.total;}
        help->setText(text("Translation coverage: %1 of %2 messages. Missing translations use English. Language packs are unverified and await native-speaker review. Use Quit and reopen to apply changes.").arg(done).arg(total));
    };
    update();
    QObject::connect(language,&QComboBox::currentIndexChanged,box,[language,update]{QSettings().setValue("i18n/language",language->currentData());update();});
    QObject::connect(format,&QComboBox::currentIndexChanged,box,[format]{QSettings().setValue("i18n/formatLocale",format->currentData());});
    return box;
}
} // namespace soundcurrent::i18n
