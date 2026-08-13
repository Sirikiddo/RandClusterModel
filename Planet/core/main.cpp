#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <windows.h>
#include <exception>

#include "core/AppViewConfig.h"
#include "dag/DagBackendBenchmark.h"
#include "ui/MainWindow.h"
#include "tests/WaterWaveModelTests.h"
#include "tests/ClimateBiomeGeneratorTests.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char** argv) {
    bool runBenchmark = false;
    bool runWaterTests = false;
    bool runClimateTests = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--benchmark") {
            runBenchmark = true;
        }
        if (QString::fromLocal8Bit(argv[i]) == "--water-tests") {
            runWaterTests = true;
        }
        if (QString::fromLocal8Bit(argv[i]) == "--climate-tests") {
            runClimateTests = true;
        }
    }
    runBenchmark = runBenchmark || QString::fromWCharArray(GetCommandLineW()).contains("--benchmark");
    runBenchmark = runBenchmark || qEnvironmentVariableIsSet("GAME_NEW_BENCHMARK");

    if (runWaterTests || runClimateTests) {
        QCoreApplication app(argc, argv);
        try {
            if (runWaterTests) runWaterWaveModelUnitTests();
            if (runClimateTests) runClimateBiomeGeneratorUnitTests();
            return 0;
        }
        catch (const std::exception&) {
            return 3;
        }
    }

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    if (runBenchmark) {
        QCoreApplication app(argc, argv);
        const QString csvPath = QDir::current().filePath("dag_backend_benchmark_results.csv");
        const DagBenchmarkReport report = runDagBackendBenchmark(csvPath);
        return report.ok ? 0 : 2;
    }

    QApplication app(argc, argv);
    const AppViewConfig viewConfig = defaultAppViewConfig();
    MainWindow w(viewConfig);

    w.resize(1280, 800);
    w.show();

    return app.exec();
}
