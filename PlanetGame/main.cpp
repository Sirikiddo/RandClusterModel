#include <QApplication>
#include <QSurfaceFormat>
#include "MainWindow.h"

int main(int argc, char** argv) {
    // Request a 4.5 Core context globally (must be done before any GL widget is created)
    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    // Uncomment if you want MSAA lines to look nicer (you can also enable per-widget)
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    MainWindow w;
    w.resize(1280, 800);
    w.show();
    return app.exec();
}
