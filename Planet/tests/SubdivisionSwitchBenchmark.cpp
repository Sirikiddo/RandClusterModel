#include "tests/SubdivisionSwitchBenchmark.h"
#include "core/AppViewConfig.h"
#include "ui/MainWindow.h"
#include "ui/HexSphereWidget.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSpinBox>
#include <QComboBox>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace {
FILE* benchmarkLog = nullptr;
std::mutex logMutex;
QElapsedTimer logClock;

void writeBenchmarkMessage(QtMsgType, const QMessageLogContext&, const QString& message) {
    const auto bytes = message.toUtf8();
    const std::lock_guard<std::mutex> lock(logMutex);
    if (benchmarkLog) {
        std::fprintf(benchmarkLog, "%.3f %s\n", logClock.nsecsElapsed() / 1000000.0, bytes.constData());
        std::fflush(benchmarkLog);
    }
}
}

int runSubdivisionSwitchBenchmark(QApplication& app, const QString& logPath) {
    QDir().mkpath(QFileInfo(logPath).absolutePath());
    benchmarkLog = _wfsopen(reinterpret_cast<const wchar_t*>(logPath.utf16()), L"wb", _SH_DENYNO);
    if (!benchmarkLog) return 7;
    logClock.start();
    const auto previousHandler = qInstallMessageHandler(writeBenchmarkMessage);
    int result = 0;
    {
#ifdef _DEBUG
        qInfo() << "[SwitchBenchmark] configuration=Debug";
#else
        qInfo() << "[SwitchBenchmark] configuration=Release";
#endif
        qInfo() << "[SwitchBenchmark] cpu_threads=" << QThread::idealThreadCount();
        qInfo() << "[SwitchBenchmark] executable=" << QCoreApplication::applicationFilePath();
        MainWindow window(defaultAppViewConfig());
        window.resize(1280, 800);
        auto* widget = window.findChild<HexSphereWidget*>();
        auto* level = window.findChild<QSpinBox*>("subdivisionLevel");
        if (!widget || !level || level->value() != 2) {
            qCritical() << "[SwitchBenchmark] Expected initial L2 controls";
            result = 8;
        } else {
            int warmFrames = 0;
            bool scheduled = false;
            bool measuring = false;
            bool completed = false;
            bool configured = false;
            QElapsedTimer switchClock;
            QObject::connect(widget, &QOpenGLWidget::frameSwapped, &window, [&]() {
                if (completed) return;
                if (measuring) {
                    widget->makeCurrent();
                    const double swapMs = switchClock.nsecsElapsed() / 1000000.0;
                    widget->context()->functions()->glFinish();
                    qInfo().nospace() << "[SwitchBenchmark] completed from=2 to=7 first_swap_ms="
                        << swapMs << " gpu_finished_ms=" << switchClock.nsecsElapsed() / 1000000.0;
                    widget->doneCurrent();
                    completed = true;
                    window.grab().save(logPath + ".png");
                    QTimer::singleShot(0, &app, &QApplication::quit);
                } else if (++warmFrames >= 3 && !scheduled) {
                    scheduled = true;
                    QTimer::singleShot(250, &window, [&]() {
                        if (!configured) {
                            configured = true;
                            window.findChild<QComboBox*>("terrainMeshWriter")->setCurrentIndex(app.arguments().contains("--direct-mesh") ? 1 : 0);
                            window.findChild<QComboBox*>("sphereGeometry")->setCurrentIndex(app.arguments().contains("--global-grid") ? 1 : 0);
                            qInfo() << "[SwitchBenchmark] global_grid=" << app.arguments().contains("--global-grid")
                                << "direct_mesh=" << app.arguments().contains("--direct-mesh");
                            warmFrames = 0;
                            scheduled = false;
                            widget->update();
                            return;
                        }
                        widget->makeCurrent();
                        auto* gl = widget->context()->functions();
                        gl->glFinish();
                        qInfo() << "[SwitchBenchmark] GPU=" << reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER))
                            << "viewport=" << widget->size() << "dpr=" << widget->devicePixelRatioF();
                        widget->doneCurrent();
                        qInfo() << "[SwitchBenchmark] BEGIN from=2 to=7";
                        measuring = true;
                        switchClock.start();
                        // One valueChanged signal: same application path as replacing 2 with 7.
                        level->setValue(7);
                        qInfo().nospace() << "[SwitchBenchmark] handler_return_ms=" << switchClock.nsecsElapsed() / 1000000.0;
                    });
                }
            });
            window.show();
            QTimer::singleShot(600000, &window, [&]() { app.exit(9); });
            result = app.exec();
            QObject::disconnect(widget, &QOpenGLWidget::frameSwapped, &window, nullptr);
            if (!completed && result == 0) result = 10;
        }
    }
    qInstallMessageHandler(previousHandler);
    std::fclose(benchmarkLog);
    benchmarkLog = nullptr;
    return result;
}
