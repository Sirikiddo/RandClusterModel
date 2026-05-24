#include <QtTest/QtTest>

#include <QDir>
#include <QFileInfo>

#include "../dag/DagBackendBenchmark.h"

class DagBackendBenchmarkTest : public QObject {
    Q_OBJECT

private slots:
    void benchmarkProducesCompatibleRows();
};

void DagBackendBenchmarkTest::benchmarkProducesCompatibleRows() {
    const QString csvPath = QDir::current().filePath("dag_backend_benchmark_results.csv");
    const DagBenchmarkReport report = runDagBackendBenchmark(csvPath, 3);

    QVERIFY(report.ok);
    QVERIFY(!report.rows.empty());
    QVERIFY(QFileInfo::exists(csvPath));

    bool sawDagTerrain = false;
    bool sawLegacyTerrain = false;
    bool sawSceneDagStats = false;
    bool sawLegacyScene = false;
    for (const auto& row : report.rows) {
        sawDagTerrain = sawDagTerrain || row.backend == "DAG terrain";
        sawLegacyTerrain = sawLegacyTerrain || row.backend == "Legacy terrain";
        sawLegacyScene = sawLegacyScene || row.backend == "Legacy scene";
        sawSceneDagStats = sawSceneDagStats || (row.backend == "DAG scene" && (row.executedNodes > 0 || row.skippedGuardNodes > 0));
    }

    QVERIFY(sawDagTerrain);
    QVERIFY(sawLegacyTerrain);
    QVERIFY(sawLegacyScene);
    QVERIFY(sawSceneDagStats);
}

QTEST_MAIN(DagBackendBenchmarkTest)
#include "dag_backend_benchmark.moc"
