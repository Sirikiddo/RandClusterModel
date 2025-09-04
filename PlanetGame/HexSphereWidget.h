#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
#include <QMatrix4x4>
#include <QPoint>
#include <QSet>
#include <QVector3D>
#include <optional>
#include <vector>

#include "HexSphereModel.h"   // IcosphereBuilder, IcoMesh, HexSphereModel
#include "TerrainTessellator.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

class HexSphereWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core
{
    Q_OBJECT
public:
    explicit HexSphereWidget(QWidget* parent = nullptr);
    ~HexSphereWidget() override;

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
    // -------- Camera --------
    float  distance_ = 2.2f;
    float  yaw_ = 0.0f;    // radians
    float  pitch_ = 0.3f;  // radians
    QPoint lastPos_;
    bool   rotating_ = false;

    QMatrix4x4 view_;
    QMatrix4x4 proj_;

    // -------- GL state flags --------
    bool glReady_ = false;   // контекст и ресурсы готовы
    bool gpuDirty_ = false;  // CPU-данные есть, но ещё не залиты в GPU

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
    float pathBias_ = 0.01f; // чуть над рельефом

    // -------- CPU model --------
    IcosphereBuilder icoBuilder_;
    IcoMesh          ico_;
    HexSphereModel   model_;
    TerrainMesh terrainCPU_; // последняя CPU-копия для пикинга

    int L_ = 2;

    // -------- Selection --------
    QSet<int> selectedCells_;

    // Параметры «планеты»
    float heightStep_ = 0.06f; // шаг высоты (радиальный)
    bool  smoothOneStep_ = true; // сглаживать при |Δ|==1
    float outlineBias_ = 0.004f; // выдавливание рамки наружу
    float stripInset_ = 0.25f; // доля отступа полосы внутрь от ребра

    // alias биома, чтобы в .h и .cpp писать Biome::Sea/Grass/Rock

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
    std::optional<int> pickCellAt(int sx, int sy); // deprecated
    std::optional<PickHit> pickTerrainAt(int sx, int sy) const;

    // GL helpers
    GLuint makeProgram(const char* vs, const char* fs);

    // утилита: безопасно установить матрицу uMVP
    inline bool setMVP(GLuint prog, GLint& cachedLoc, const QMatrix4x4& mvp) {
        if (!prog || !glIsProgram(prog)) return false;
        glUseProgram(prog);
        if (cachedLoc < 0) {
            cachedLoc = glGetUniformLocation(prog, "uMVP");
        }
        if (cachedLoc >= 0) {
            glUniformMatrix4fv(cachedLoc, 1, GL_FALSE, mvp.constData());
            return true;
        }
        else {
            qWarning("uMVP location is -1 for program %u", prog);
            return false;
        }
    }
};
