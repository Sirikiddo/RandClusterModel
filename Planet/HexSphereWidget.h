// HexSphereWidget.h - переработанная версия с отдельными классами
#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QMatrix4x4>
#include <QPoint>
#include <QQuaternion>
#include <QSet>
#include <QVector3D>
#include <QTimer>
#include <optional>
#include <memory>

#include "HexSphereSceneController.h"
#include "HexSphereRenderer.h"
#include "PerformanceStats.h"
#include "scene/SceneGraph.h"
#include "SceneEntity.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

class HexSphereWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit HexSphereWidget(QWidget* parent = nullptr);
    ~HexSphereWidget() override;

public slots:
    void setSubdivisionLevel(int L);
    void resetView();
    void clearSelection();
    void setTerrainParams(const TerrainParams& p) { scene_.setGenParams(p); }
    void setGeneratorByIndex(int idx) { scene_.setGeneratorByIndex(idx); }
    void regenerateTerrain();

    void setSmoothOneStep(bool on) { scene_.setSmoothOneStep(on); }
    void setStripInset(float v) { scene_.setStripInset(v); }
    void setOutlineBias(float v) { scene_.setOutlineBias(v); }

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
    struct PickHit {
        int cellId;
        int entityId;
        QVector3D pos;
        float t;
        bool isEntity;
    };

    // Camera controls
    void updateCamera();
    QVector3D rayOrigin() const;
    QVector3D rayDirectionFromScreen(int sx, int sy) const;

    // Picking helpers
    std::optional<int> pickCellAt(int sx, int sy);
    std::optional<PickHit> pickTerrainAt(int sx, int sy) const;
    std::optional<PickHit> pickEntityAt(int sx, int sy) const;
    std::optional<PickHit> pickSceneAt(int sx, int sy) const;

    void selectEntity(int entityId);
    void deselectEntity();
    void moveSelectedEntityToCell(int cellId);

    void rebuildModel();
    void uploadSelection();
    void uploadBuffers();
    void buildAndShowSelectedPath();
    void clearPath();
    void updateBufferUsageStrategy();

    HexSphereSceneController scene_{};
    HexSphereRenderer renderer_{ this };
    scene::SceneGraph sceneGraph_{};

    PerformanceStats stats_{};

    float distance_ = 2.2f;
    QPoint lastPos_;
    bool rotating_ = false;
    QQuaternion sphereRotation_;

    QMatrix4x4 view_;
    QMatrix4x4 proj_;

    HexSphereRenderer::UploadOptions uploadOptions_{};

    int selectedEntityId_ = -1;

    QTimer* waterTimer_ = nullptr;
    float waterTime_ = 0.0f;
    QVector3D lightDir_ = QVector3D(1, 1, 1).normalized();
};
