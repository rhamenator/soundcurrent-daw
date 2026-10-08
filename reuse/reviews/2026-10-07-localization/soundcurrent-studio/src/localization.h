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
    for(const auto &l:supported) if(l.tag.compare(tag,Qt::CaseInsensitive)==0) return l.tag;
    // Strip only regional subtags, keeping explicit scripts (e.g. sr-Latn).
    auto parts=tag.split('-');
    while(parts.size()>1) {
        if(parts.last().size()==4) break;
        parts.removeLast();const auto base=parts.join('-');
        for(const auto &l:supported) if(l.tag.compare(base,Qt::CaseInsensitive)==0) return l.tag;
    }
    return "en";
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
        if(translator_)QCoreApplication::installTranslator(translator_.get());
        const auto direction=loaded_=="qps-rtl"?Qt::RightToLeft:QLocale(loaded_).textDirection();
        QApplication::setLayoutDirection(direction);
    }
    ~Runtime(){if(translator_)QCoreApplication::removeTranslator(translator_.get());}
    QString requested() const{return requested_;} QString loaded() const{return loaded_;}
private: QString requested_,loaded_;std::unique_ptr<QTranslator> translator_;
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
