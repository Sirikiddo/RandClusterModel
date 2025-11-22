// HexSphereWidget.h - исправленная объединенная версия
#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QPoint>
#include <QSet>
#include <QVector3D>
#include <QTime>
#include <QQuaternion>
#include <QTimer>
#include <optional>
#include <vector>
#include <memory>

#include "HexSphereModel.h"
#include "TerrainTessellator.h"
#include "TerrainGenerator.h"
#include "SceneGraph.h"
#include "PathBuilder.h"
#include "ModelHandler.h"
#include "PerformanceStats.h"
#include "scene/Transform.h"

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
    void setTerrainParams(const TerrainParams& p) { genParams_ = p; }
    void setGeneratorByIndex(int idx); // 0 NoOp, 1 Sine, 2 Perlin, 3 Climate
    void regenerateTerrain();

    // визуальные настройки
    void setSmoothOneStep(bool on) { smoothOneStep_ = on; }
    void setStripInset(float v) { stripInset_ = std::clamp(v, 0.f, 0.49f); }
    void setOutlineBias(float v) { outlineBias_ = std::max(0.f, v); }

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
    // Статистика производительности
    PerformanceStats stats_;

    // Флаги оптимизации
    bool useStaticBuffers_ = true;
    GLenum terrainBufferUsage_ = GL_STATIC_DRAW;
    GLenum wireBufferUsage_ = GL_STATIC_DRAW;

    // Методы оптимизации
    void updateBufferUsageStrategy();
    void optimizeTerrainBuffers();

    // -------- Water & Environment --------
    GLuint envCubemap_ = 0;
    GLint uEnvMap_ = -1;
    void generateEnvCubemap();

    QTimer* waterTimer_ = nullptr;
    float waterTime_ = 0.0f; // накапливаемое время для анимации

    // -------- Lighting & Water --------
    GLuint progWater_ = 0;
    GLint uMVP_Water_ = -1, uTime_Water_ = -1, uLightDir_Water_ = -1, uViewPos_Water_ = -1;
    GLuint vboTerrainNorm_ = 0;
    GLint uModel_ = -1;
    GLint uLightDir_ = -1;
    QVector3D lightDir_ = QVector3D(1, 1, 1).normalized();

    // -------- Camera --------
    float  distance_ = 2.2f;
    float  yaw_ = 0.0f;    // radians (сохранено для совместимости)
    float  pitch_ = 0.3f;  // radians (сохранено для совместимости)
    QPoint lastPos_;
    bool   rotating_ = false;
    QQuaternion sphereRotation_;  // текущее вращение планеты

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

    // -------- 3D Models --------
    std::shared_ptr<ModelHandler> treeModel_;
    GLuint progModel_ = 0;
    GLuint progModelTextured_ = 0;
    GLint uMVP_Model_ = -1;
    GLint uModel_Model_ = -1;
    GLint uLightDir_Model_ = -1;
    GLint uViewPos_Model_ = -1;
    GLint uColor_Model_ = -1;
    GLint uUseTexture_ = -1;

    // -------- CPU model --------
    IcosphereBuilder icoBuilder_;
    IcoMesh          ico_;
    HexSphereModel   model_;
    TerrainMesh terrainCPU_;

    // SceneGraph
    SceneGraph scene_;
    scene::CoordinateFrame globalFrame_{};

    int L_ = 2;

    // -------- Selection --------
    QSet<int> selectedCells_;
    int selectedEntityId_ = -1; // Для управления выделением объектов

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
    struct PickHit {
        int cellId;
        int entityId; // ID объекта сцены (-1 если не объект)
        QVector3D pos;
        float t;
        bool isEntity; // true если попали в объект, false если в ландшафт
    };

    void      updateCamera();
    QVector3D rayOrigin() const;
    QVector3D rayDirectionFromScreen(int sx, int sy) const;
    std::optional<int> pickCellAt(int sx, int sy);
    std::optional<PickHit> pickTerrainAt(int sx, int sy) const;
    std::optional<PickHit> pickEntityAt(int sx, int sy) const;
    std::optional<PickHit> pickSceneAt(int sx, int sy) const; // объединяет ландшафт и объекты

    // GL helpers
    GLuint makeProgram(const char* vs, const char* fs);

    // SceneGraph & Water
    void initPyramidGeometry();
    void createWaterGeometry();

    // 3D Models helpers
    void orientToSurfaceNormal(QMatrix4x4& matrix, const QVector3D& normal);
    QVector3D getSurfacePoint(int cellId) const;

    // Управление выделением объектов
    void selectEntity(int entityId);
    void deselectEntity();
    void moveSelectedEntityToCell(int cellId);

    // Автоматический расчет heightStep
    float autoHeightStep() const {
        const float baseStep = 0.05f;
        const float reductionFactor = 0.4f;
        return baseStep / (1.0f + L_ * reductionFactor);
    }
};