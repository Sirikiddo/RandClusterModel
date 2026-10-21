#pragma once
#include <QString>

// CPU backend timings and complete result digests; excludes GUI/GPU rendering.
bool runDagTransportBenchmark(const QString& csvPath);
