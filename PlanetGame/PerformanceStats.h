#ifndef PERFORMANCESTATS_H
#define PERFORMANCESTATS_H

#include <QElapsedTimer>
#include <QString>
#include <QOpenGLExtraFunctions>

class PerformanceStats {
public:
    PerformanceStats();

    void frameRendered();
    void startGPUTimer();
    void stopGPUTimer();
    void updateMemoryStats(GLsizei vertexCount, GLsizei indexCount, GLsizei triangleCount);
    void setSubdivisionLevel(int L) { L_ = L; }

    int fps() const { return currentFPS_; }
    double gpuTime() const { return gpuTime_; }
    GLsizei vertices() const { return currentVertices_; }
    GLsizei indices() const { return currentIndices_; }
    GLsizei triangles() const { return currentTriangles_; }

    QString getStatsString() const;
    QString getDetailedStats() const;

private:
    QElapsedTimer fpsTimer_;
    QElapsedTimer gpuTimer_;
    int frameCount_ = 0;
    int currentFPS_ = 0;
    double gpuTime_ = 0.0;
    GLsizei currentVertices_ = 0;
    GLsizei currentIndices_ = 0;
    GLsizei currentTriangles_ = 0;
    int L_ = 2; // Уровень подразделения
};

#endif // PERFORMANCESTATS_H