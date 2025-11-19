#include "PerformanceStats.h"
#include <QDebug>

PerformanceStats::PerformanceStats() {
    fpsTimer_.start();
    gpuTimer_.start();
}

void PerformanceStats::frameRendered() {
    frameCount_++;
    if (fpsTimer_.elapsed() >= 1000) {
        currentFPS_ = frameCount_;
        frameCount_ = 0;
        fpsTimer_.restart();
    }
}

void PerformanceStats::startGPUTimer() {
    gpuTimer_.start();
}

void PerformanceStats::stopGPUTimer() {
    gpuTime_ = gpuTimer_.nsecsElapsed() / 1000000.0;
    gpuTimer_.restart();
}

void PerformanceStats::updateMemoryStats(GLsizei vertexCount, GLsizei indexCount, GLsizei triangleCount) {
    currentVertices_ = vertexCount;
    currentIndices_ = indexCount;
    currentTriangles_ = triangleCount;
}

QString PerformanceStats::getStatsString() const {
    return QString("FPS: %1 | GPU: %2ms | Verts: %3 | Tris: %4 | L: %5")
        .arg(currentFPS_)
        .arg(gpuTime_, 0, 'f', 2)
        .arg(currentVertices_)
        .arg(currentTriangles_)
        .arg(L_);
}

QString PerformanceStats::getDetailedStats() const {
    // –асчет примерного использовани€ пам€ти
    size_t approxMemory = (currentVertices_ * 3 * sizeof(float)) + // позиции
        (currentVertices_ * 3 * sizeof(float)) + // цвета  
        (currentVertices_ * 3 * sizeof(float)) + // нормали
        (currentIndices_ * sizeof(uint32_t));    // индексы

    return QString("=== PERFORMANCE DETAILS ===\n"
        "FPS: %1\n"
        "GPU Time: %2 ms\n"
        "Vertices: %3\n"
        "Indices: %4\n"
        "Triangles: %5\n"
        "Subdivision Level: L%6\n"
        "Approx. Memory: %7 KB")
        .arg(currentFPS_)
        .arg(gpuTime_, 0, 'f', 3)
        .arg(currentVertices_)
        .arg(currentIndices_)
        .arg(currentTriangles_)
        .arg(L_)
        .arg(approxMemory / 1024);
}