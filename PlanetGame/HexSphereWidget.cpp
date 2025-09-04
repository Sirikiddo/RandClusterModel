// HexSphereWidget.cpp — bind‑path (GL 3.3+), упрощённый
// Правка: все «стены» (обрывы без сглаживания) ориентируются наружу → не пропадают при CULL_FACE

#include "HexSphereWidget.h"
#include "PathBuilder.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector2D>
#include <QOpenGLContext>
#include <algorithm>
#include <limits>
#include <string>
#include <cmath>

// ─── Шейдеры ───────────────────────────────────────────────────────────────────
static const char* VS_WIRE = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos,1.0); }
)GLSL";

static const char* FS_WIRE = R"GLSL(
#version 450 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,0.0,0.0,1.0); }
)GLSL";

static const char* VS_TERRAIN = R"GLSL(
#version 450 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main(){ vColor = aColor; gl_Position = uMVP * vec4(aPos,1.0); }
)GLSL";

static const char* FS_TERRAIN = R"GLSL(
#version 450 core
in vec3 vColor;
out vec4 FragColor;
void main(){ FragColor = vec4(vColor, 1.0); }
)GLSL";

static const char* FS_SEL = R"GLSL(
#version 450 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,1.0,0.2,1.0); }
)GLSL";

// ─── Жизненный цикл ─────────────────────────────────────────────────────────────
HexSphereWidget::HexSphereWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

HexSphereWidget::~HexSphereWidget() {
    makeCurrent();
    if (progWire_)    this->glDeleteProgram(progWire_);
    if (progTerrain_) this->glDeleteProgram(progTerrain_);
    if (progSel_)     this->glDeleteProgram(progSel_);
    if (vaoWire_)     this->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoTerrain_)  this->glDeleteVertexArrays(1, &vaoTerrain_);
    if (vaoSel_)      this->glDeleteVertexArrays(1, &vaoSel_);
    if (vboPositions_)   this->glDeleteBuffers(1, &vboPositions_);
    if (vboTerrainPos_)  this->glDeleteBuffers(1, &vboTerrainPos_);
    if (vboTerrainCol_)  this->glDeleteBuffers(1, &vboTerrainCol_);
    if (iboTerrain_)     this->glDeleteBuffers(1, &iboTerrain_);
    if (vboSel_)         this->glDeleteBuffers(1, &vboSel_);
    if (vaoPath_) this->glDeleteVertexArrays(1, &vaoPath_);
    if (vboPath_) this->glDeleteBuffers(1, &vboPath_);
    doneCurrent();
}

GLuint HexSphereWidget::makeProgram(const char* vs, const char* fs) {
    GLuint v = this->glCreateShader(GL_VERTEX_SHADER);
    this->glShaderSource(v, 1, &vs, nullptr);
    this->glCompileShader(v);
    GLuint f = this->glCreateShader(GL_FRAGMENT_SHADER);
    this->glShaderSource(f, 1, &fs, nullptr);
    this->glCompileShader(f);
    GLuint p = this->glCreateProgram();
    this->glAttachShader(p, v); this->glAttachShader(p, f);
    this->glLinkProgram(p);
    this->glDeleteShader(v); this->glDeleteShader(f);
    return p;
}

void HexSphereWidget::initializeGL() {
    initializeOpenGLFunctions();
    this->glEnable(GL_DEPTH_TEST);
    this->glEnable(GL_CULL_FACE);
    this->glCullFace(GL_BACK);
    this->glFrontFace(GL_CCW);

    progWire_ = makeProgram(VS_WIRE, FS_WIRE);
    progTerrain_ = makeProgram(VS_TERRAIN, FS_TERRAIN);
    progSel_ = makeProgram(VS_WIRE, FS_SEL);

    this->glUseProgram(progWire_);    uMVP_Wire_ = this->glGetUniformLocation(progWire_, "uMVP");
    this->glUseProgram(progTerrain_); uMVP_Terrain_ = this->glGetUniformLocation(progTerrain_, "uMVP");
    this->glUseProgram(progSel_);     uMVP_Sel_ = this->glGetUniformLocation(progSel_, "uMVP");
    this->glUseProgram(0);

    // ── Буферы/VAO: bind‑path ─────────────────────────────────────────────────
    this->glGenBuffers(1, &vboPositions_);
    this->glGenVertexArrays(1, &vaoWire_);

    this->glGenBuffers(1, &vboTerrainPos_);
    this->glGenBuffers(1, &vboTerrainCol_);
    this->glGenBuffers(1, &iboTerrain_);
    this->glGenVertexArrays(1, &vaoTerrain_);

    this->glGenBuffers(1, &vboSel_);
    this->glGenVertexArrays(1, &vaoSel_);

    // wire
    this->glBindVertexArray(vaoWire_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindVertexArray(0);

    // terrain
    this->glBindVertexArray(vaoTerrain_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    this->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(1);
    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    this->glBindVertexArray(0);

    // selection
    this->glBindVertexArray(vaoSel_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboSel_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindVertexArray(0);

	// path
    this->glGenBuffers(1, &vboPath_);
    this->glGenVertexArrays(1, &vaoPath_);
    this->glBindVertexArray(vaoPath_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboPath_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindVertexArray(0);

    glReady_ = true;
    if (gpuDirty_) { uploadWireBuffers(); uploadTerrainBuffers(); uploadSelectionOutlineBuffers(); gpuDirty_ = false; }
    else { rebuildModel(); }
}

void HexSphereWidget::resizeGL(int w, int h) {
    const float dpr = devicePixelRatioF();
    const int   pw = int(w * dpr);
    const int   ph = int(h * dpr);
    proj_.setToIdentity();
    proj_.perspective(50.0f, float(pw) / float(std::max(ph, 1)), 0.01f, 50.0f);
}

void HexSphereWidget::paintGL() {
    const float dpr = devicePixelRatioF();
    this->glViewport(0, 0, int(width() * dpr), int(height() * dpr));
    this->glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    this->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    updateCamera();
    const QMatrix4x4 mvp = proj_ * view_;

    if (terrainIndexCount_ > 0 && progTerrain_) {
        this->glUseProgram(progTerrain_);
        this->glUniformMatrix4fv(uMVP_Terrain_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoTerrain_);
        this->glDrawElements(GL_TRIANGLES, terrainIndexCount_, GL_UNSIGNED_INT, nullptr);
        this->glBindVertexArray(0);
    }
    if (selLineVertexCount_ > 0 && progSel_) {
        this->glUseProgram(progSel_);
        this->glUniformMatrix4fv(uMVP_Sel_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoSel_);
        this->glDrawArrays(GL_LINES, 0, selLineVertexCount_);
        this->glBindVertexArray(0);
    }
    if (lineVertexCount_ > 0 && progWire_) {
        this->glUseProgram(progWire_);
        this->glUniformMatrix4fv(uMVP_Wire_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoWire_);
        this->glDrawArrays(GL_LINES, 0, lineVertexCount_);
        this->glBindVertexArray(0);
    }
    if (pathVertexCount_ > 0 && progWire_) {
        this->glUseProgram(progWire_);
        this->glUniformMatrix4fv(uMVP_Wire_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoPath_);
        this->glDrawArrays(GL_LINE_STRIP, 0, pathVertexCount_);
        this->glBindVertexArray(0);
    }
}

// ─── Build/Upload ─────────────────────────────────────────────────────────────
void HexSphereWidget::rebuildModel() {
    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);
    if (glReady_) { uploadWireBuffers(); uploadTerrainBuffers(); uploadSelectionOutlineBuffers(); update(); }
    else { gpuDirty_ = true; }
}

void HexSphereWidget::uploadWireBuffers() {
    if (!glReady_) { gpuDirty_ = true; return; }
    makeCurrent();
    const auto& V = model_.dualVerts();
    std::vector<float> lineVerts; lineVerts.reserve(model_.wireEdges().size() * 6);
    for (auto [a, b] : model_.wireEdges()) {
        const auto& pa = V[size_t(a)]; const auto& pb = V[size_t(b)];
        lineVerts.insert(lineVerts.end(), { pa.x(),pa.y(),pa.z(), pb.x(),pb.y(),pb.z() });
    }
    this->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    this->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lineVerts.size() * sizeof(float)), lineVerts.data(), GL_STATIC_DRAW);
    lineVertexCount_ = GLsizei(lineVerts.size() / 3);
    doneCurrent();
}

// Ориентирует треугольник так, чтобы нормаль смотрела приблизительно в сторону refOut (наружу)
static inline void orientOut(QVector3D& A, QVector3D& B, QVector3D& C, const QVector3D& refOut) {
    QVector3D n = QVector3D::crossProduct(B - A, C - A);
    if (QVector3D::dotProduct(n, refOut) < 0.0f) std::swap(B, C);
}

void HexSphereWidget::uploadTerrainBuffers() {
    if (!glReady_) { gpuDirty_ = true; return; }
    makeCurrent();

    TerrainTessellator tt;
    tt.R = 1.0f;
    tt.heightStep = heightStep_;
    tt.inset = stripInset_;
    tt.smoothMaxDelta = smoothOneStep_;

    TerrainMesh m = tt.build(model_);
    terrainCPU_ = std::move(m); // сохранить для пикинга

    const GLsizeiptr vbPos = GLsizeiptr(terrainCPU_.pos.size() * sizeof(float));
    const GLsizeiptr vbCol = GLsizeiptr(terrainCPU_.col.size() * sizeof(float));
    const GLsizeiptr ib = GLsizeiptr(terrainCPU_.idx.size() * sizeof(uint32_t));

    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    this->glBufferData(GL_ARRAY_BUFFER, vbPos, terrainCPU_.pos.empty() ? nullptr : terrainCPU_.pos.data(), GL_DYNAMIC_DRAW);

    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    this->glBufferData(GL_ARRAY_BUFFER, vbCol, terrainCPU_.col.empty() ? nullptr : terrainCPU_.col.data(), GL_DYNAMIC_DRAW);

    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    this->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, terrainCPU_.idx.empty() ? nullptr : terrainCPU_.idx.data(), GL_DYNAMIC_DRAW);

    this->glBindBuffer(GL_ARRAY_BUFFER, 0);
    terrainIndexCount_ = GLsizei(terrainCPU_.idx.size());
    doneCurrent();
}

void HexSphereWidget::uploadSelectionOutlineBuffers() {
    if (!glReady_) { gpuDirty_ = true; return; }
    makeCurrent();

    const auto& cells = model_.cells();
    const auto& dual = model_.dualVerts();
    constexpr float R = 1.0f;

    std::vector<float> lineVerts; lineVerts.reserve(selectedCells_.size() * 12);

    auto liftBias = [&](const QVector3D& u, float h) { return u.normalized() * (R + h * heightStep_ + outlineBias_); };

    for (int cid : selectedCells_) {
        const auto& c = cells[size_t(cid)];
        const int deg = int(c.poly.size());
        for (int i = 0; i < deg; ++i) {
            const int j = (i + 1) % deg;
            const int va = c.poly[i];
            const int vb = c.poly[j];
            float hA = float(c.height), hB = float(c.height);
            const int nEdge = c.neighbors[i];
            if (nEdge >= 0) {
                const int hN = cells[size_t(nEdge)].height;
                const int d = std::abs(hN - c.height);
                if (smoothOneStep_ && d == 1) { const float mid = 0.5f * float(hN + c.height); hA = hB = mid; }
            }
            const QVector3D pA = liftBias(dual[size_t(va)], hA);
            const QVector3D pB = liftBias(dual[size_t(vb)], hB);
            lineVerts.insert(lineVerts.end(), { pA.x(),pA.y(),pA.z(), pB.x(),pB.y(),pB.z() });
        }
    }

    this->glBindBuffer(GL_ARRAY_BUFFER, vboSel_);
    this->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lineVerts.size() * sizeof(float)), lineVerts.data(), GL_DYNAMIC_DRAW);
    selLineVertexCount_ = GLsizei(lineVerts.size() / 3);
    doneCurrent();
}

void HexSphereWidget::uploadPathBuffer(const std::vector<QVector3D>& pts) {
    if (!glReady_) { gpuDirty_ = true; return; }
    makeCurrent();
    std::vector<float> buf; buf.reserve(pts.size() * 3);
    for (auto& p : pts) { buf.push_back(p.x()); buf.push_back(p.y()); buf.push_back(p.z()); }
    this->glBindBuffer(GL_ARRAY_BUFFER, vboPath_);
    this->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(buf.size() * sizeof(float)),
        buf.empty() ? nullptr : buf.data(), GL_DYNAMIC_DRAW);
    pathVertexCount_ = GLsizei(buf.size() / 3);
    doneCurrent();
}

// найдёт путь между двумя выбранными ячейками (ровно 2 в выделении) и нарисует
void HexSphereWidget::buildAndShowSelectedPath() {
    if (selectedCells_.size() != 2) return;
    auto it = selectedCells_.begin();
    int a = *it; ++it; int b = *it;

    PathBuilder pb(model_);
    pb.build(); // веса = 1
    auto ids = pb.astar(a, b);
    auto poly = pb.polylineOnSphere(ids, /*segmentsPerEdge=*/8, pathBias_, heightStep_);
    uploadPathBuffer(poly);
    update();
}

void HexSphereWidget::clearPath() { pathVertexCount_ = 0; update(); }

// ─── Камера/Инпут ─────────────────────────────────────────────────────────────
void HexSphereWidget::updateCamera() {
    view_.setToIdentity();
    QMatrix4x4 rot; rot.setToIdentity();
    rot.rotate(qRadiansToDegrees(pitch_), 1, 0, 0);
    rot.rotate(qRadiansToDegrees(yaw_), 0, 1, 0);
    const QVector3D eye = rot.map(QVector3D(0, 0, distance_));
    const QVector3D up = rot.map(QVector3D(0, 1, 0));
    view_.lookAt(eye, QVector3D(0, 0, 0), up);
}

QVector3D HexSphereWidget::rayOrigin() const {
    const QMatrix4x4 invView = view_.inverted();
    return (invView.map(QVector4D(0, 0, 0, 1))).toVector3D();
}

QVector3D HexSphereWidget::rayDirectionFromScreen(int sx, int sy) const {
    const float dpr = devicePixelRatioF();
    const float w = float(width() * dpr);
    const float h = float(height() * dpr);
    const float x = 2.0f * (float(sx) * dpr / w) - 1.0f;
    const float y = 1.0f - 2.0f * (float(sy) * dpr / h);
    const QMatrix4x4 inv = (proj_ * view_).inverted();
    QVector4D pNear = inv.map(QVector4D(x, y, -1.0f, 1.0f));
    QVector4D pFar = inv.map(QVector4D(x, y, 1.0f, 1.0f));
    pNear /= pNear.w(); pFar /= pFar.w();
    return (pFar.toVector3D() - pNear.toVector3D()).normalized();
}

static inline bool rayTriangleMT(const QVector3D& o, const QVector3D& d,
    const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
    float& tOut) {
    const float EPS = 1e-6f;
    const QVector3D e1 = v1 - v0;
    const QVector3D e2 = v2 - v0;
    const QVector3D p = QVector3D::crossProduct(d, e2);
    const float det = QVector3D::dotProduct(e1, p);
    if (std::fabs(det) < EPS) return false;
    const float invDet = 1.0f / det;
    const QVector3D t = o - v0;
    const float u = QVector3D::dotProduct(t, p) * invDet; if (u < -EPS || u > 1.0f + EPS) return false;
    const QVector3D q = QVector3D::crossProduct(t, e1);
    const float v = QVector3D::dotProduct(d, q) * invDet; if (v < -EPS || u + v > 1.0f + EPS) return false;
    const float tt = QVector3D::dotProduct(e2, q) * invDet; if (tt <= EPS) return false;
    tOut = tt; return true;
}

std::optional<int> HexSphereWidget::pickCellAt(int sx, int sy) {
    const QVector3D ro = rayOrigin();
    const QVector3D rd = rayDirectionFromScreen(sx, sy);
    const auto& tris = model_.pickTris();
    float bestT = std::numeric_limits<float>::infinity();
    int bestId = -1;
    for (const auto& pt : tris) { float t; if (rayTriangleMT(ro, rd, pt.v0, pt.v1, pt.v2, t)) if (t < bestT) { bestT = t; bestId = pt.cellId; } }
    if (bestId >= 0) return bestId; else return std::nullopt;
}

std::optional<HexSphereWidget::PickHit> HexSphereWidget::pickTerrainAt(int sx, int sy) const {
    if (terrainCPU_.triOwner.empty()) return std::nullopt;
    const QVector3D ro = rayOrigin();
    const QVector3D rd = rayDirectionFromScreen(sx, sy);

    float bestT = std::numeric_limits<float>::infinity();
    int   bestOwner = -1;
    QVector3D bestPos;

    const auto& P = terrainCPU_.pos;
    const auto& I = terrainCPU_.idx;
    const auto& O = terrainCPU_.triOwner;

    const size_t triCount = O.size();
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = I[3 * t + 0], i1 = I[3 * t + 1], i2 = I[3 * t + 2];
        const QVector3D v0(P[3 * i0], P[3 * i0 + 1], P[3 * i0 + 2]);
        const QVector3D v1(P[3 * i1], P[3 * i1 + 1], P[3 * i1 + 2]);
        const QVector3D v2(P[3 * i2], P[3 * i2 + 1], P[3 * i2 + 2]);
        float tt;
        if (rayTriangleMT(ro, rd, v0, v1, v2, tt) && tt < bestT) {
            bestT = tt; bestOwner = O[t]; bestPos = ro + rd * tt;
        }
    }
    if (bestOwner >= 0) return PickHit{ bestOwner, bestPos, bestT };
    return std::nullopt;
}

void HexSphereWidget::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    lastPos_ = e->pos();
    if (e->button() == Qt::RightButton) { rotating_ = true; }
    else if (e->button() == Qt::LeftButton) {
        if (!glReady_) return;
        if (auto hit = pickTerrainAt(e->pos().x(), e->pos().y())) {
            int cid = hit->cellId;
            if (selectedCells_.contains(cid)) selectedCells_.remove(cid);
            else selectedCells_.insert(cid);
            uploadSelectionOutlineBuffers();
            update();
        }
    }
}

void HexSphereWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!rotating_) return;
    const QPoint d = e->pos() - lastPos_;
    lastPos_ = e->pos();
    const float s = 0.005f;
    yaw_ += d.x() * s;  pitch_ += d.y() * s;  pitch_ = std::clamp(pitch_, -1.5f, 1.5f);
    update();
}

void HexSphereWidget::mouseReleaseEvent(QMouseEvent* e) { if (e->button() == Qt::RightButton) rotating_ = false; }

void HexSphereWidget::wheelEvent(QWheelEvent* e) { const float steps = (e->angleDelta().y() / 8.0f) / 15.0f; distance_ *= std::pow(0.9f, steps); distance_ = std::clamp(distance_, 1.2f, 10.0f); update(); }

// ─── API ───────────────────────────────────────────────────────────────────────
void HexSphereWidget::setSubdivisionLevel(int L) { L_ = std::max(0, L); rebuildModel(); }
void HexSphereWidget::resetView() { distance_ = 2.2f; yaw_ = 0.0f; pitch_ = 0.3f; update(); }
void HexSphereWidget::clearSelection() { selectedCells_.clear(); if (glReady_) { uploadTerrainBuffers(); uploadSelectionOutlineBuffers(); update(); } else { gpuDirty_ = true; } }

void HexSphereWidget::keyPressEvent(QKeyEvent* e) {
    // Клавиши без зависимости от выделения
    switch (e->key()) 
    {
    case Qt::Key_C:  clearPath(); return;                  // очистить путь всегда
    case Qt::Key_S:  smoothOneStep_ = !smoothOneStep_;     // переключатель сглаживания
        uploadTerrainBuffers(); update(); return;
    case Qt::Key_P:  buildAndShowSelectedPath(); return;   // сам проверит, что выбрано 2 клетки
    default: break;
    }

    // Ниже — только операции, требующие выделения
    if (selectedCells_.empty()) return;

    auto apply = [&](auto fn) { for (int cid : selectedCells_) fn(cid); };
    switch (e->key())
    {
    case Qt::Key_Plus:
    case Qt::Key_Equal:      apply([&](int cid) { model_.addHeight(cid, +1); }); break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore: apply([&](int cid) { model_.addHeight(cid, -1); }); break;
    case Qt::Key_1:          apply([&](int cid) { model_.setBiome(cid, Biome::Sea);   }); break;
    case Qt::Key_2:          apply([&](int cid) { model_.setBiome(cid, Biome::Grass); }); break;
    case Qt::Key_3:          apply([&](int cid) { model_.setBiome(cid, Biome::Rock);  }); break;
    default: return;
    }
    uploadTerrainBuffers();
    uploadSelectionOutlineBuffers();
    update();
}
