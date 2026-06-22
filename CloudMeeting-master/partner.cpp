#include "partner.h"
#include <QDebug>
#include <QEvent>
#include <QHostAddress>
Partner::Partner(QWidget *parent, quint32 p):QLabel(parent)
{
//    qDebug() <<"dsaf" << this->parent();
    ip = p;

    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    int pw = parent ? parent->size().width() : 100;
    if (pw <= 10) pw = 100;
    w = pw;
    this->setPixmap(QPixmap::fromImage(QImage(":/myImage/1.jpg").scaled(w - 10, w - 10)));
    this->setFrameShape(QFrame::Box);

    this->setStyleSheet("border-width: 1px; border-style: solid; border-color: #334155; border-radius: 4px;");
//    this->setToolTipDuration(5);

    this->setToolTip(QHostAddress(ip).toString());
}


void Partner::mousePressEvent(QMouseEvent *)
{
    emit sendip(ip);
}
void Partner::setpic(QImage img)
{
    // 动态获取当前父控件宽度，避免窗口缩放后尺寸不匹配
    QWidget *pw = (QWidget *)this->parent();
    int cw = pw ? pw->size().width() : 100;
    if (cw <= 10) cw = 100;
    this->setPixmap(QPixmap::fromImage(img.scaled(cw - 10, cw - 10, Qt::KeepAspectRatio, Qt::FastTransformation)));
}
