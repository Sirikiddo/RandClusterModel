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
    bool sawSelectionL2 = false;
    bool sawSelectionL4 = false;
    for (const auto& row : report.rows) {
        sawDagTerrain = sawDagTerrain || row.backend == "DAG terrain";
        sawLegacyTerrain = sawLegacyTerrain || row.backend == "Legacy terrain";
        sawLegacyScene = sawLegacyScene || row.backend == "Legacy scene";
        sawSceneDagStats = sawSceneDagStats || (row.backend == "DAG scene" && (row.executedNodes > 0 || row.skippedGuardNodes > 0));
        sawSelectionL2 = sawSelectionL2 || (row.backend == "DAG selection" && row.scenario == "L2"
            && row.inputBytes > 0 && (row.executedNodes > 0 || row.skippedGuardNodes > 0));
        sawSelectionL4 = sawSelectionL4 || (row.backend == "DAG selection" && row.scenario == "L4"
            && row.inputBytes > 0 && (row.executedNodes > 0 || row.skippedGuardNodes > 0));
    }

    QVERIFY(sawDagTerrain);
    QVERIFY(sawLegacyTerrain);
    QVERIFY(sawLegacyScene);
    QVERIFY(sawSceneDagStats);
    QVERIFY(sawSelectionL2);
    QVERIFY(sawSelectionL4);
}

QTEST_MAIN(DagBackendBenchmarkTest)
#include "dag_backend_benchmark.moc"
