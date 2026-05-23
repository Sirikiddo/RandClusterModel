#include <QApplication>
#include <QSurfaceFormat>
#include <windows.h>

#include "core/AppViewConfig.h"
#include "ui/MainWindow.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    const AppViewConfig viewConfig = defaultAppViewConfig();
    MainWindow w(viewConfig);

    w.resize(1280, 800);
    w.show();

    return app.exec();
}
