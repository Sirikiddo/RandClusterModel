/********************************************************************************
** Form generated from reading UI file 'Planet3.ui'
**
** Created by: Qt User Interface Compiler version 6.8.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PLANET3_H
#define UI_PLANET3_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Planet3Class
{
public:
    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QWidget *centralWidget;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *Planet3Class)
    {
        if (Planet3Class->objectName().isEmpty())
            Planet3Class->setObjectName("Planet3Class");
        Planet3Class->resize(600, 400);
        menuBar = new QMenuBar(Planet3Class);
        menuBar->setObjectName("menuBar");
        Planet3Class->setMenuBar(menuBar);
        mainToolBar = new QToolBar(Planet3Class);
        mainToolBar->setObjectName("mainToolBar");
        Planet3Class->addToolBar(mainToolBar);
        centralWidget = new QWidget(Planet3Class);
        centralWidget->setObjectName("centralWidget");
        Planet3Class->setCentralWidget(centralWidget);
        statusBar = new QStatusBar(Planet3Class);
        statusBar->setObjectName("statusBar");
        Planet3Class->setStatusBar(statusBar);

        retranslateUi(Planet3Class);

        QMetaObject::connectSlotsByName(Planet3Class);
    } // setupUi

    void retranslateUi(QMainWindow *Planet3Class)
    {
        Planet3Class->setWindowTitle(QCoreApplication::translate("Planet3Class", "Planet3", nullptr));
    } // retranslateUi

};

namespace Ui {
    class Planet3Class: public Ui_Planet3Class {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PLANET3_H
