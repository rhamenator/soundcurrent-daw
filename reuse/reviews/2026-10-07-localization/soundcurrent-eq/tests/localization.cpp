// SPDX-License-Identifier: GPL-3.0-only
#include "localization.h"
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTemporaryDir>
#include <stdexcept>
using namespace soundcurrent::i18n;
void require(bool pass,const char *why){if(!pass)throw std::runtime_error(why);}
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setOrganizationName("SoundCurrent");app.setApplicationName("localization-test");
 QTemporaryDir dir;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
 try{
  require(languages().size()>=30,"Global language catalogs missing");
  require(resolve("fr_CA")=="fr","Regional fallback failed");
  require(resolve("pt-BR")=="pt-BR" && resolve("pt-PT")=="pt-PT","Distinct Portuguese regions collapsed");
  require(resolve("zh-Hant-TW")=="zh-Hant" && resolve("zh-Hans-CN")=="zh-Hans","Chinese scripts collapsed");
  require(resolve("sr-Latn")=="en","An absent explicit script was guessed");
  require(resolve("../bad")=="en","Unsupported language should fall back safely");
  for(const auto &l:languages()) {
   QTranslator translator;require(translator.load(":/i18n/soundcurrent_"+l.tag+".qm"),"Embedded catalog failed to load");
   require(!translator.translate("SoundCurrent","Equalizer").isEmpty(),"Draft language is empty");
  }
  QSettings().setValue("i18n/language","de");QSettings().setValue("i18n/formatLocale","de-DE");
  {
   Runtime runtime;runtime.initialize();require(text("Output device")==QString::fromUtf8("Ausgabegerät"),"German catalog not active");
   require(text("Untranslated sample")=="Untranslated sample","English fallback failed");
   std::unique_ptr<QGroupBox> panel(settingsPanel());
   auto *format=panel->findChild<QComboBox *>("formatLocale");
   require(format->findData("de-DE")>=0 && format->findData("fr-CA")>=0,"Explicit formatting regions missing");
   QDoubleSpinBox spin;spin.setRange(-60,12);spin.setDecimals(1);spin.setValue(1.5);
   require(spin.text().contains(','),"Decimal comma missing");
   spin.findChild<QLineEdit *>()->setText("-12,5");spin.interpretText();require(spin.value()==-12.5,"Localized numeric input changed value");
   auto json=QJsonDocument(QJsonObject{{"gain",spin.value()},{"presetId","Flat"}}).toJson(QJsonDocument::Compact);
   require(json.contains("-12.5") && !json.contains("-12,5"),"Machine state became locale-dependent");
  }
  {
   QSettings().setValue("i18n/language","qps-rtl");Runtime runtime;runtime.initialize();
   auto pattern=text("Value %1 / %2");require(pattern.contains("%1")&&pattern.contains("%2"),"Pseudo locale damaged placeholders");
   require(QApplication::layoutDirection()==Qt::RightToLeft,"RTL layout missing");
  }
  qInfo("PASS: embedded global catalogs, regional/script fallback, decimal input, invariant JSON and RTL placeholders");return 0;
 }catch(const std::exception&e){qCritical("%s",e.what());return 1;}
}
