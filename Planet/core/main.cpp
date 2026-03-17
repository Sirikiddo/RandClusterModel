#include <QApplication>
#include <QSurfaceFormat>
#include <windows.h>
#include "ui/MainWindow.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char** argv) {
    // СНАЧАЛА устанавливаем формат (до QApplication!)
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4); // MSAA
    QSurfaceFormat::setDefaultFormat(fmt);

    qDebug() << "1. Format set";

    // ПОТОМ создаём QApplication
    QApplication app(argc, argv);
    qDebug() << "2. App created";

    // И только потом виджеты
    MainWindow w;
    qDebug() << "3. MainWindow created";

    w.resize(1280, 800);
    qDebug() << "4. Window resized";

    w.show();
    qDebug() << "5. Window shown";

    return app.exec();
}