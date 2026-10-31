#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <windows.h>
#include <exception>
#include <fstream>

#include "core/AppViewConfig.h"
#include "dag/DagBackendBenchmark.h"
#include "ui/MainWindow.h"
#include "tests/WaterWaveModelTests.h"
#include "tests/WaterScreenPassTests.h"
#include "tests/ClimateBiomeGeneratorTests.h"
#include "tests/DagTransportBenchmark.h"
#include "tests/RiverGeneratorTests.h"
#include "tests/TopologyTests.h"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--dag-transport-benchmark") {
            QCoreApplication app(argc, argv);
            const QString path = i + 1 < argc ? QString::fromLocal8Bit(argv[i + 1])
                : QDir::current().filePath("dag_transport_benchmark.csv");
            return runDagTransportBenchmark(path) ? 0 : 4;
        }
    }
    bool runBenchmark = false;
    bool runWaterTests = false;
    bool runClimateTests = false;
    bool runRiverTests = false;
    bool runTopologyTests = false;
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
        if (QString::fromLocal8Bit(argv[i]) == "--river-tests") {
            runRiverTests = true;
        }
        if (QString::fromLocal8Bit(argv[i]) == "--topology-tests") {
            runTopologyTests = true;
        }
    }
    runBenchmark = runBenchmark || QString::fromWCharArray(GetCommandLineW()).contains("--benchmark");
    runBenchmark = runBenchmark || qEnvironmentVariableIsSet("GAME_NEW_BENCHMARK");

    if (runWaterTests || runClimateTests || runRiverTests || runTopologyTests) {
        QCoreApplication app(argc, argv);
        try {
            if (runWaterTests) runWaterWaveModelUnitTests();
            if (runClimateTests) runClimateBiomeGeneratorUnitTests();
            if (runRiverTests) runRiverGeneratorUnitTests();
            if (runTopologyTests) runTopologyUnitTests();
            return 0;
        }
        catch (const std::exception& error) {
            std::ofstream("unit_test_failure.txt") << error.what() << std::endl;
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
    if (app.arguments().contains("--water-screen-benchmark")) {
        const int index = app.arguments().indexOf("--water-screen-benchmark");
        const QString prefix = index + 1 < app.arguments().size() ? app.arguments().at(index + 1)
            : QStringLiteral("build/Planet/water-screen");
        return runWaterScreenPassBenchmark(prefix) ? 0 : 6;
    }
    if (app.arguments().contains("--water-screen-tests")) {
        return runWaterScreenPassTests() ? 0 : 5;
    }
    const AppViewConfig viewConfig = defaultAppViewConfig();
    MainWindow w(viewConfig);

    w.resize(1280, 800);
    w.show();

    return app.exec();
}
