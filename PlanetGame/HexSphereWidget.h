#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QPoint>
#include <QSet>
#include <QVector3D>
#include <QTime>
#include <optional>
#include <vector>
#include <memory>

#include "HexSphereModel.h"
#include "TerrainTessellator.h"
#include "TerrainGenerator.h"
#include "SceneGraph.h"
#include "PathBuilder.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

class HexSphereWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit HexSphereWidget(QWidget* parent = nullptr);
    ~HexSphereWidget() override;

    // Новые методы для управления генераторами рельефа
    void setGenerator(std::unique_ptr<ITerrainGenerator> g) { generator_ = std::move(g); }
    void setGenParams(const TerrainParams& p) { genParams_ = p; }

public slots:
    void setSubdivisionLevel(int L);
    void resetView();
    void clearSelection();

signals:
    void hudTextChanged(const QString&);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    // -------- Lighting & Water --------
    GLuint progWater_ = 0;
    GLint uMVP_Water_ = -1, uTime_Water_ = -1, uLightDir_Water_ = -1, uViewPos_Water_ = -1;
    GLuint vboTerrainNorm_ = 0;
    GLint uModel_ = -1;
    GLint uLightDir_ = -1;
    QVector3D lightDir_ = QVector3D(1, 1, 1).normalized();

    // -------- Camera --------
    float  distance_ = 2.2f;
    float  yaw_ = 0.0f;    // radians
    float  pitch_ = 0.3f;  // radians
    QPoint lastPos_;
    bool   rotating_ = false;

    QMatrix4x4 view_;
    QMatrix4x4 proj_;

    // -------- GL state flags --------
    bool glReady_ = false;
    bool gpuDirty_ = false;

    // -------- GL programs + uniform locations --------
    GLuint progWire_ = 0, progTerrain_ = 0, progSel_ = 0;
    GLint  uMVP_Wire_ = -1, uMVP_Terrain_ = -1, uMVP_Sel_ = -1;

    // -------- VAOs/VBOs --------
    // wire
    GLuint vaoWire_ = 0, vboPositions_ = 0;
    GLsizei lineVertexCount_ = 0;

    // terrain
    GLuint vaoTerrain_ = 0, vboTerrainPos_ = 0, vboTerrainCol_ = 0, iboTerrain_ = 0;
    GLsizei terrainIndexCount_ = 0;

    // selection outline
    GLuint vaoSel_ = 0, vboSel_ = 0;
    GLsizei selLineVertexCount_ = 0;

    // path
    GLuint vboPath_ = 0, vaoPath_ = 0;
    GLsizei pathVertexCount_ = 0;
    float pathBias_ = 0.01f;

    // pyramid (объекты сцены)
    GLuint vaoPyramid_ = 0;
    GLuint vboPyramid_ = 0;
    GLsizei pyramidVertexCount_ = 0;

    // water
    GLuint vaoWater_ = 0, vboWaterPos_ = 0, iboWater_ = 0;
    GLsizei waterIndexCount_ = 0;

    // -------- CPU model --------
    IcosphereBuilder icoBuilder_;
    IcoMesh          ico_;
    HexSphereModel   model_;
    TerrainMesh terrainCPU_;

    // SceneGraph
    SceneGraph scene_;

    int L_ = 2;

    // -------- Selection --------
    QSet<int> selectedCells_;

    // -------- Параметры «планеты» --------
    float heightStep_ = 0.06f;
    bool  smoothOneStep_ = true;
    float outlineBias_ = 0.004f;
    float stripInset_ = 0.25f;

    // -------- Генераторы рельефа --------
    std::unique_ptr<ITerrainGenerator> generator_;
    TerrainParams genParams_;

private:
    // Построение CPU-модели и загрузка в GPU
    void rebuildModel();
    void uploadWireBuffers();
    void uploadTerrainBuffers();
    void uploadSelectionOutlineBuffers();
    void uploadPathBuffer(const std::vector<QVector3D>& pts);

    void buildAndShowSelectedPath();
    void clearPath();

    // Камера/пикинг
    struct PickHit { int cellId; QVector3D pos; float t; };

    void      updateCamera();
    QVector3D rayOrigin() const;
    QVector3D rayDirectionFromScreen(int sx, int sy) const;
    std::optional<int> pickCellAt(int sx, int sy);
    std::optional<PickHit> pickTerrainAt(int sx, int sy) const;

    // GL helpers
    GLuint makeProgram(const char* vs, const char* fs);

    // SceneGraph & Water
    void initPyramidGeometry();
    void createWaterGeometry();

    // Автоматический расчет heightStep
    float autoHeightStep() const {
        const float baseStep = 0.05f;
        const float reductionFactor = 0.4f;
        return baseStep / (1.0f + L_ * reductionFactor);
    }
};