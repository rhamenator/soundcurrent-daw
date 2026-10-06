// SPDX-License-Identifier: GPL-3.0-only
#include "accelerating_spinbox.h"
#include <QApplication>
#include <QThread>
#include <iostream>
#include <stdexcept>
void require(bool v,const char *why){if(!v)throw std::runtime_error(why);}
void press(QWidget &box,int key,bool repeat=false){QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier,QString(),repeat);QApplication::sendEvent(&box,&event);}
void release(QWidget &box,int key){QKeyEvent event(QEvent::KeyRelease,key,Qt::NoModifier);QApplication::sendEvent(&box,&event);}
class MouseProbe : public soundcurrent::AcceleratingSpinBox {
public:
 QPoint upArrow(){QStyleOptionSpinBox option;initStyleOption(&option);return style()->subControlRect(QStyle::CC_SpinBox,&option,QStyle::SC_SpinBoxUp,this).center();}
 void repeat(){stepBy(1);}
};
int main(int argc,char **argv){QApplication app(argc,argv);try{
 using namespace soundcurrent;require(heldSpinMultiplier(0)==1&&heldSpinMultiplier(.35)==1,"initial delay");
 double previous=1;for(int i=0;i<=1000;++i){const double speed=heldSpinMultiplier(i*.01);require(speed>=previous&&speed>=1&&speed<=16,"bounded monotonic curve");previous=speed;}
 require(heldSpinMultiplier(10)>15.99,"speed cap");
 AcceleratingSpinBox box;box.setRange(-10000,10000);box.setSingleStep(1);
 press(box,Qt::Key_Up);require(box.value()==1,"single press normal step");
 QThread::msleep(1400);const int before=box.value();press(box,Qt::Key_Up,true);require(box.value()-before>=8,"held acceleration");
 release(box,Qt::Key_Up);const int reset=box.value();press(box,Qt::Key_Down);require(box.value()==reset-1,"release and direction reset");release(box,Qt::Key_Down);
 AcceleratingDoubleSpinBox floating;floating.setRange(-100,100);floating.setDecimals(2);floating.setSingleStep(.1);
 press(floating,Qt::Key_Up);release(floating,Qt::Key_Up);require(std::abs(floating.value()-.1)<1e-9,"floating click step");
 MouseProbe mouse;mouse.setRange(-10000,10000);mouse.resize(100,32);mouse.show();app.processEvents();const auto pos=mouse.upArrow();
 QMouseEvent down(QEvent::MouseButtonPress,QPointF(pos),QPointF(mouse.mapToGlobal(pos)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&mouse,&down);require(mouse.value()==1,"mouse click normal step");
 QThread::msleep(1400);const int mouseBefore=mouse.value();mouse.repeat();require(mouse.value()-mouseBefore>=8,"mouse held acceleration");
 QMouseEvent up(QEvent::MouseButtonRelease,QPointF(pos),QPointF(mouse.mapToGlobal(pos)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&mouse,&up);const int mouseReset=mouse.value();mouse.repeat();require(mouse.value()==mouseReset+1,"mouse release reset");
 std::cout<<"PASS: half-Gaussian acceleration, bounded speed, hold, release/direction reset, integer and decimal steps\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
