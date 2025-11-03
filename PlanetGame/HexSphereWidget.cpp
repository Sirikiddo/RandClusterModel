#include "HexSphereWidget.h"
#include "TerrainGenerator.h"
#include "PathBuilder.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QVector2D>
#include <QOpenGLContext>
#include <algorithm>
#include <limits>
#include <string>
#include <cmath>
#include <QLabel>

// ─── Шейдеры ───────────────────────────────────────────────────────────────────
static const char* VS_WIRE = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos,1.0); }
)GLSL";

static const char* FS_WIRE = R"GLSL(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,0.0,0.0,1.0); }
)GLSL";

static const char* VS_TERRAIN = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uView;

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    // Мировые координаты вершины
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Нормаль трансформируем корректно (матрицей без масштабирования)
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;

    vColor = aColor;

    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_TERRAIN = R"GLSL(
#version 330 core
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;

out vec4 FragColor;

uniform vec3 uLightDir;   // направление света в мировых координатах
uniform vec3 uViewPos;    // позиция камеры (для будущего specular)

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);  // если uLightDir — это направление света, а не вектор к источнику

    // diffuse по Ламберту
    float diff = max(dot(N, L), 0.0);

    // добавим немного ambient, чтобы не было полностью чёрных теней
    vec3 ambient = 0.3 * vColor;
    vec3 diffuse = 0.7 * diff * vColor;

    vec3 result = ambient + diffuse;
    FragColor = vec4(result, 1.0);
}
)GLSL";

static const char* FS_SEL = R"GLSL(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,1.0,0.2,1.0); }
)GLSL";

// ─── Water shader ─────────────────────────────────────────────────────────────
static const char* VS_WATER = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
uniform float uTime;
out vec3 vPos;
void main() {
    vPos = aPos;
    // слегка "дышит" по синусу
    float wave = sin(aPos.x*4.0 + uTime*1.2) * 0.02
               + sin(aPos.z*3.0 + uTime*0.7) * 0.02;
    vec3 displaced = aPos + normalize(aPos) * wave;
    gl_Position = uMVP * vec4(displaced, 1.0);
}
)GLSL";

static const char* FS_WATER = R"GLSL(
#version 330 core
in vec3 vPos;
out vec4 FragColor;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform float uTime;

void main() {
    // нормаль как слегка колеблющаяся по шуму (приближённо)
    vec3 N = normalize(vPos + vec3(
        0.05*sin(vPos.z*6.0 + uTime*1.0),
        0.05*sin(vPos.x*6.0 + uTime*0.8),
        0.05*sin(vPos.y*6.0 + uTime*1.3)
    ));
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uViewPos - vPos);
    vec3 R = reflect(-L, N);

    float diff = max(dot(N,L),0.0);
    float spec = pow(max(dot(V,R),0.0), 64.0);

    vec3 base = vec3(0.0,0.3,0.6);
    vec3 ambient = 0.3 * base;
    vec3 diffuse = 0.5 * diff * base;
    vec3 specular = 0.8 * spec * vec3(1.0);

    FragColor = vec4(ambient + diffuse + specular, 0.8);
}
)GLSL";

// ─── Жизненный цикл ─────────────────────────────────────────────────────────────
HexSphereWidget::HexSphereWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    auto* hud = new QLabel(this);
    hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud->setStyleSheet("QLabel { background: rgba(0,0,0,140); color: white; padding: 6px; }");
    hud->move(10, 10);
    hud->setText("LMB: select | C: clear path | P: build path | +/-: height | 1–3: biomes | S: smooth | W: move");
    hud->adjustSize();
}

HexSphereWidget::~HexSphereWidget() {
    makeCurrent();
    // ... (очистка ресурсов как в версии 2)
    if (progWire_)    this->glDeleteProgram(progWire_);
    if (progTerrain_) this->glDeleteProgram(progTerrain_);
    if (progSel_)     this->glDeleteProgram(progSel_);
    if (progWater_)   this->glDeleteProgram(progWater_);
    if (vaoWire_)     this->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoTerrain_)  this->glDeleteVertexArrays(1, &vaoTerrain_);
    if (vaoSel_)      this->glDeleteVertexArrays(1, &vaoSel_);
    if (vaoPath_)     this->glDeleteVertexArrays(1, &vaoPath_);
    if (vaoWater_)    this->glDeleteVertexArrays(1, &vaoWater_);
    if (vaoPyramid_)  this->glDeleteVertexArrays(1, &vaoPyramid_);
    if (vboPositions_)   this->glDeleteBuffers(1, &vboPositions_);
    if (vboTerrainPos_)  this->glDeleteBuffers(1, &vboTerrainPos_);
    if (vboTerrainCol_)  this->glDeleteBuffers(1, &vboTerrainCol_);
    if (vboTerrainNorm_) this->glDeleteBuffers(1, &vboTerrainNorm_);
    if (iboTerrain_)     this->glDeleteBuffers(1, &iboTerrain_);
    if (vboSel_)         this->glDeleteBuffers(1, &vboSel_);
    if (vboPath_)        this->glDeleteBuffers(1, &vboPath_);
    if (vboPyramid_)     this->glDeleteBuffers(1, &vboPyramid_);
    if (vboWaterPos_)    this->glDeleteBuffers(1, &vboWaterPos_);
    if (iboWater_)       this->glDeleteBuffers(1, &iboWater_);
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

    // Компилируем шейдеры
    progWire_ = makeProgram(VS_WIRE, FS_WIRE);
    progTerrain_ = makeProgram(VS_TERRAIN, FS_TERRAIN);
    progSel_ = makeProgram(VS_WIRE, FS_SEL);
    progWater_ = makeProgram(VS_WATER, FS_WATER);

    // Получаем uniform locations
    this->glUseProgram(progWire_);    uMVP_Wire_ = this->glGetUniformLocation(progWire_, "uMVP");
    this->glUseProgram(progTerrain_);
    uMVP_Terrain_ = this->glGetUniformLocation(progTerrain_, "uMVP");
    uModel_ = this->glGetUniformLocation(progTerrain_, "uModel");
    uLightDir_ = this->glGetUniformLocation(progTerrain_, "uLightDir");
    this->glUseProgram(progSel_);     uMVP_Sel_ = this->glGetUniformLocation(progSel_, "uMVP");
    this->glUseProgram(progWater_);
    uMVP_Water_ = this->glGetUniformLocation(progWater_, "uMVP");
    uTime_Water_ = this->glGetUniformLocation(progWater_, "uTime");
    uLightDir_Water_ = this->glGetUniformLocation(progWater_, "uLightDir");
    uViewPos_Water_ = this->glGetUniformLocation(progWater_, "uViewPos");
    this->glUseProgram(0);

    // Инициализация генератора рельефа по умолчанию
    setGenerator(std::make_unique<PerlinTerrainGenerator>());
    setGenParams(TerrainParams{ /*seed=*/0u, /*seaLevel=*/0, /*scale=*/3.0f });

    // Создаем VAO/VBO
    this->glGenBuffers(1, &vboPositions_);
    this->glGenVertexArrays(1, &vaoWire_);
    this->glGenBuffers(1, &vboTerrainPos_);
    this->glGenBuffers(1, &vboTerrainCol_);
    this->glGenBuffers(1, &vboTerrainNorm_);
    this->glGenBuffers(1, &iboTerrain_);
    this->glGenVertexArrays(1, &vaoTerrain_);
    this->glGenBuffers(1, &vboSel_);
    this->glGenVertexArrays(1, &vaoSel_);
    this->glGenBuffers(1, &vboPath_);
    this->glGenVertexArrays(1, &vaoPath_);
    this->glGenBuffers(1, &vboWaterPos_);
    this->glGenBuffers(1, &iboWater_);
    this->glGenVertexArrays(1, &vaoWater_);

    // === КРИТИЧЕСКИ ВАЖНО: НАСТРОЙКА VERTEX ARRAYS ===

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
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    this->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(2);
    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    this->glBindVertexArray(0);

    // selection
    this->glBindVertexArray(vaoSel_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboSel_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindVertexArray(0);

    // path
    this->glBindVertexArray(vaoPath_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboPath_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindVertexArray(0);

    // water
    this->glBindVertexArray(vaoWater_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);
    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    this->glBindVertexArray(0);

    // Инициализация SceneGraph
    SceneEntity pyramid;
    pyramid.name = "Explorer";
    pyramid.meshId = "pyramid";
    pyramid.currentCell = 0;
    pyramid.position = QVector3D(0, 0, 1);
    pyramid.selected = true;
    scene_.addEntity(pyramid);
    initPyramidGeometry();

    glReady_ = true;
    rebuildModel();

    emit hudTextChanged(
        "Controls: [LMB] select | [C] clear path | [P] path between selected | "
        "[+/-] height | [1–3] biome | [S] smooth toggle | [W] move entity");
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

    // Рендеринг terrain с освещением
    if (terrainIndexCount_ > 0 && progTerrain_) {
        this->glUseProgram(progTerrain_);
        this->glUniformMatrix4fv(uMVP_Terrain_, 1, GL_FALSE, mvp.constData());
        QMatrix4x4 model; model.setToIdentity();
        this->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());
        this->glUniform3f(uLightDir_, lightDir_.x(), lightDir_.y(), lightDir_.z());
        this->glBindVertexArray(vaoTerrain_);
        this->glDrawElements(GL_TRIANGLES, terrainIndexCount_, GL_UNSIGNED_INT, nullptr);
        this->glBindVertexArray(0);
    }

    // Рендеринг воды
    if (waterIndexCount_ > 0 && progWater_) {
        this->glUseProgram(progWater_);
        this->glUniformMatrix4fv(uMVP_Water_, 1, GL_FALSE, mvp.constData());
        this->glUniform1f(uTime_Water_, float(QTime::currentTime().msecsSinceStartOfDay()) * 0.001f);
        QVector3D lightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
        this->glUniform3f(uLightDir_Water_, lightDir.x(), lightDir.y(), lightDir.z());
        QVector3D eye = (view_.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
        this->glUniform3f(uViewPos_Water_, eye.x(), eye.y(), eye.z());
        this->glEnable(GL_BLEND);
        this->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        this->glBindVertexArray(vaoWater_);
        this->glDrawElements(GL_TRIANGLES, waterIndexCount_, GL_UNSIGNED_INT, nullptr);
        this->glBindVertexArray(0);
        this->glDisable(GL_BLEND);
    }

    // Рендеринг объектов сцены (пирамиды)
    for (const auto& e : scene_.entities()) {
        QMatrix4x4 model;
        model.translate(e.position);
        model.scale(0.05f);
        const QMatrix4x4 mvp = proj_ * view_ * model;
        this->glUseProgram(progWire_);
        this->glUniformMatrix4fv(uMVP_Wire_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoPyramid_);
        this->glDrawArrays(GL_TRIANGLES, 0, pyramidVertexCount_);
    }

    // Рендеринг выделения, путей и wireframe (код из версий 1 и 2)
    if (selLineVertexCount_ > 0 && progSel_) {
        this->glUseProgram(progSel_);
        this->glUniformMatrix4fv(uMVP_Sel_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoSel_);
        this->glDrawArrays(GL_LINES, 0, selLineVertexCount_);
    }
    if (lineVertexCount_ > 0 && progWire_) {
        this->glUseProgram(progWire_);
        this->glUniformMatrix4fv(uMVP_Wire_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoWire_);
        this->glDrawArrays(GL_LINES, 0, lineVertexCount_);
    }
    if (pathVertexCount_ > 0 && progWire_) {
        this->glUseProgram(progWire_);
        this->glUniformMatrix4fv(uMVP_Wire_, 1, GL_FALSE, mvp.constData());
        this->glBindVertexArray(vaoPath_);
        this->glDrawArrays(GL_LINE_STRIP, 0, pathVertexCount_);
    }
}

// ─── Build/Upload ─────────────────────────────────────────────────────────────
void HexSphereWidget::rebuildModel() {
    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);

    // ГЕНЕРАЦИЯ РЕЛЬЕФА - ключевое улучшение из версии 1
    if (generator_) {
        generator_->generate(model_, genParams_);
    }

    if (glReady_) {
        uploadWireBuffers();
        uploadTerrainBuffers();
        uploadSelectionOutlineBuffers();
        update();
    }
    else {
        gpuDirty_ = true;
    }
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

void HexSphereWidget::uploadTerrainBuffers() {
    if (!glReady_) { gpuDirty_ = true; return; }
    makeCurrent();

    TerrainTessellator tt;
    tt.R = 1.0f;

    // Адаптивный heightStep в зависимости от уровня детализации (из версии 1)
    heightStep_ = autoHeightStep();
    tt.heightStep = heightStep_;

    tt.inset = stripInset_;
    tt.smoothMaxDelta = smoothOneStep_ ? 1 : 0;
    tt.outerTrim = 0.15f;

    // Включаем все этапы рендеринга для плавности
    tt.doCaps = true;
    tt.doBlades = true;
    tt.doCornerTris = true;
    tt.doEdgeCliffs = true;

    TerrainMesh m = tt.build(model_);
    terrainCPU_ = std::move(m);

    const GLsizeiptr vbPos = GLsizeiptr(terrainCPU_.pos.size() * sizeof(float));
    const GLsizeiptr vbCol = GLsizeiptr(terrainCPU_.col.size() * sizeof(float));
    const GLsizeiptr vbNorm = GLsizeiptr(terrainCPU_.norm.size() * sizeof(float));
    const GLsizeiptr ib = GLsizeiptr(terrainCPU_.idx.size() * sizeof(uint32_t));

    // Позиции
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    this->glBufferData(GL_ARRAY_BUFFER, vbPos, terrainCPU_.pos.empty() ? nullptr : terrainCPU_.pos.data(), GL_DYNAMIC_DRAW);

    // Цвета
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    this->glBufferData(GL_ARRAY_BUFFER, vbCol, terrainCPU_.col.empty() ? nullptr : terrainCPU_.col.data(), GL_DYNAMIC_DRAW);

    // Нормали (ДОБАВЛЕНО из версии 2 для освещения)
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    this->glBufferData(GL_ARRAY_BUFFER, vbNorm, terrainCPU_.norm.empty() ? nullptr : terrainCPU_.norm.data(), GL_DYNAMIC_DRAW);

    // Индексы
    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    this->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, terrainCPU_.idx.empty() ? nullptr : terrainCPU_.idx.data(), GL_DYNAMIC_DRAW);

    this->glBindBuffer(GL_ARRAY_BUFFER, 0);
    terrainIndexCount_ = GLsizei(terrainCPU_.idx.size());

    // Создаем геометрию воды (из версии 2)
    createWaterGeometry();

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

// ─── SceneGraph объекты ──────────────────────────────────────────────────────
void HexSphereWidget::initPyramidGeometry() {
    // Вершины пирамиды (основание + вершина)
    std::vector<float> pyramidVerts = {
        // Основание (квадрат)
        -0.5f, 0.0f, -0.5f,  // зад-лев
         0.5f, 0.0f, -0.5f,  // зад-прав
         0.5f, 0.0f,  0.5f,  // перед-прав
        -0.5f, 0.0f,  0.5f,  // перед-лев

        // Вершина
        0.0f, 1.0f, 0.0f     // верх
    };

    // Индексы для треугольников
    std::vector<uint32_t> pyramidIndices = {
        // Основание (2 треугольника)
        0, 1, 2,
        0, 2, 3,

        // Боковые грани
        0, 1, 4,
        1, 2, 4,
        2, 3, 4,
        3, 0, 4
    };

    // Конвертируем индексы в вершины для простоты отрисовки
    std::vector<float> pyramidVertices;
    pyramidVertices.reserve(pyramidIndices.size() * 3);

    for (uint32_t idx : pyramidIndices) {
        pyramidVertices.push_back(pyramidVerts[idx * 3]);
        pyramidVertices.push_back(pyramidVerts[idx * 3 + 1]);
        pyramidVertices.push_back(pyramidVerts[idx * 3 + 2]);
    }

    // Создаем VAO и VBO для пирамиды
    this->glGenVertexArrays(1, &vaoPyramid_);
    this->glGenBuffers(1, &vboPyramid_);

    this->glBindVertexArray(vaoPyramid_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboPyramid_);
    this->glBufferData(GL_ARRAY_BUFFER,
        pyramidVertices.size() * sizeof(float),
        pyramidVertices.data(),
        GL_STATIC_DRAW);

    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);

    this->glBindVertexArray(0);

    pyramidVertexCount_ = static_cast<GLsizei>(pyramidVertices.size() / 3);
}

void HexSphereWidget::createWaterGeometry() {
    const auto& cells = model_.cells();
    const auto& dual = model_.dualVerts();

    std::vector<float> waterPos;
    std::vector<uint32_t> waterIndices;

    // Собираем вершины и индексы только для водных ячеек
    for (const auto& cell : cells) {
        if (cell.biome != Biome::Sea) continue;

        if (cell.poly.size() < 3) continue;

        // Центр ячейки (немного выше для избежания z-fighting)
        QVector3D center = cell.centroid.normalized() * (1.0f + cell.height * heightStep_ + 0.001f);
        int centerIndex = waterPos.size() / 3;
        waterPos.insert(waterPos.end(), { center.x(), center.y(), center.z() });

        // Вершины полигона (также приподнятые)
        std::vector<int> vertexIndices;
        for (int dv : cell.poly) {
            const QVector3D& vert = dual[size_t(dv)];
            QVector3D waterVert = vert.normalized() * (1.0f + cell.height * heightStep_ + 0.001f);
            vertexIndices.push_back(waterPos.size() / 3);
            waterPos.insert(waterPos.end(), { waterVert.x(), waterVert.y(), waterVert.z() });
        }

        // Создаем треугольники веером от центра
        int numVertices = vertexIndices.size();
        for (int i = 0; i < numVertices; ++i) {
            waterIndices.push_back(centerIndex);
            waterIndices.push_back(vertexIndices[i]);
            waterIndices.push_back(vertexIndices[(i + 1) % numVertices]);
        }
    }

    // Загружаем данные в буферы воды
    this->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    this->glBufferData(GL_ARRAY_BUFFER,
        waterPos.size() * sizeof(float),
        waterPos.empty() ? nullptr : waterPos.data(),
        GL_STATIC_DRAW);

    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    this->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        waterIndices.size() * sizeof(uint32_t),
        waterIndices.empty() ? nullptr : waterIndices.data(),
        GL_STATIC_DRAW);

    waterIndexCount_ = static_cast<GLsizei>(waterIndices.size());
}

// ─── Пути и навигация ────────────────────────────────────────────────────────
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

// ─── Обработка ввода ─────────────────────────────────────────────────────────
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

void HexSphereWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) rotating_ = false;
}

void HexSphereWidget::wheelEvent(QWheelEvent* e) {
    const float steps = (e->angleDelta().y() / 8.0f) / 15.0f;
    distance_ *= std::pow(0.9f, steps);
    distance_ = std::clamp(distance_, 1.2f, 10.0f);
    update();
}

// ─── Улучшенная обработка клавиш ─────────────────────────────────────────────
void HexSphereWidget::keyPressEvent(QKeyEvent* e) {
    // Клавиши без зависимости от выделения
    switch (e->key()) {
    case Qt::Key_C:
        clearPath();
        return;
    case Qt::Key_S:
        smoothOneStep_ = !smoothOneStep_;
        uploadTerrainBuffers();
        update();
        emit hudTextChanged("Smooth mode: " + QString(smoothOneStep_ ? "ON" : "OFF"));
        return;
    case Qt::Key_P:
        buildAndShowSelectedPath();
        return;
    case Qt::Key_W:  // Навигация объектов сцены
    {
        auto sel = scene_.getSelectedEntity();
        if (!sel) return;
        SceneEntity& ent = sel->get();
        const auto& cells = model_.cells();
        if (ent.currentCell < 0 || ent.currentCell >= (int)cells.size()) return;
        const auto& c = cells[(size_t)ent.currentCell];
        if (c.neighbors.empty()) return;
        int next = c.neighbors[0]; // просто первый сосед
        if (next < 0) return;
        ent.currentCell = next;
        ent.position = cells[(size_t)next].centroid.normalized();
        update();
    }
    return;
    default: break;
    }

    // Операции, требующие выделения
    if (selectedCells_.empty()) return;

    auto apply = [&](auto fn) { for (int cid : selectedCells_) fn(cid); };
    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        apply([&](int cid) { model_.addHeight(cid, +1); });
        break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        apply([&](int cid) { model_.addHeight(cid, -1); });
        break;
    case Qt::Key_1:
        apply([&](int cid) { model_.setBiome(cid, Biome::Sea);   });
        break;
    case Qt::Key_2:
        apply([&](int cid) { model_.setBiome(cid, Biome::Grass); });
        break;
    case Qt::Key_3:
        apply([&](int cid) { model_.setBiome(cid, Biome::Rock);  });
        break;
    default: return;
    }
    uploadTerrainBuffers();
    uploadSelectionOutlineBuffers();
    update();
}

// ─── API ─────────────────────────────────────────────────────────────────────
void HexSphereWidget::setSubdivisionLevel(int L) {
    if (L != L_) {
        L_ = L;
        // Автоматически пересчитываем шаг высоты для нового уровня
        heightStep_ = autoHeightStep();
        rebuildModel();
        update();
    }
}

void HexSphereWidget::resetView() {
    distance_ = 2.2f;
    yaw_ = 0.0f;
    pitch_ = 0.3f;
    update();
}

void HexSphereWidget::clearSelection() {
    selectedCells_.clear();
    if (glReady_) {
        uploadTerrainBuffers();
        uploadSelectionOutlineBuffers();
        update();
    }
    else {
        gpuDirty_ = true;
    }
}

// ─── Пикинг ──────────────────────────────────────────────────────────────────
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
    for (const auto& pt : tris) {
        float t;
        if (rayTriangleMT(ro, rd, pt.v0, pt.v1, pt.v2, t))
            if (t < bestT) { bestT = t; bestId = pt.cellId; }
    }
    if (bestId >= 0) return bestId;
    else return std::nullopt;
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

void HexSphereWidget::setGeneratorByIndex(int idx) {
    switch (idx) {
    case 0: setGenerator(std::make_unique<NoOpTerrainGenerator>()); break;
    case 1: setGenerator(std::make_unique<SineTerrainGenerator>()); break;
    default: setGenerator(std::make_unique<PerlinTerrainGenerator>()); break;
    }
}

void HexSphereWidget::regenerateTerrain() {
    if (generator_) {
        generator_->generate(model_, genParams_);
    }
    if (glReady_) {
        uploadTerrainBuffers();
        uploadSelectionOutlineBuffers();
        update();
    }
    else {
        gpuDirty_ = true;
    }
}