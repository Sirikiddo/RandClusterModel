#pragma once

#include <vector>

#include <QString>

struct DagBenchmarkRow {
    QString category;
    QString scenario;
    QString operation;
    QString backend;
    int iteration = 0;
    double elapsedMs = 0.0;
    int cellCount = 0;
    bool compatible = true;
    int selectionCount = 0;
    int treeCount = 0;
    int modelCount = 0;
    int executedNodes = 0;
    int skippedGuardNodes = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
};

struct DagBenchmarkReport {
    QString csvPath;
    bool ok = true;
    std::vector<DagBenchmarkRow> rows;
};

DagBenchmarkReport runDagBackendBenchmark(const QString& csvPath, int iterations = 5);
