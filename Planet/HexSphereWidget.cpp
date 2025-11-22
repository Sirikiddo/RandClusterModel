#include "HexSphereWidget.h"
#include "PathBuilder.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QVector2D>
#include <QOpenGLContext>
#include <algorithm>
#include <limits>
#include <string>
#include <cmath>
#include <QLabel>
#include <functional>
#include <iostream>

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
uniform mat4 uModel;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

// Шум для более сложных волн
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // Генерация текстурных координат из позиции
    vTexCoord = aPos.xz * 2.0;
    
    // Многослойный шум для сложных волн
    float wave1 = sin(aPos.x * 4.0 + uTime * 1.5) * 0.02;
    float wave2 = sin(aPos.z * 3.0 + uTime * 1.2) * 0.015;
    
    // Добавляем шум Перлина для более естественных волн
    float noiseWave = noise(vec2(aPos.x * 3.0 + uTime * 0.8, aPos.z * 3.0)) * 0.01;
    float noiseWave2 = noise(vec2(aPos.x * 6.0 - uTime * 0.6, aPos.z * 6.0)) * 0.005;
    
    // Комбинируем все волны
    float totalWave = wave1 + wave2 + noiseWave + noiseWave2;
    
    // Смещение по нормали (вверх)
    vec3 displaced = aPos + vec3(0.0, totalWave, 0.0);
    vWorldPos = displaced;
    
    // Вычисление нормали для волн
    float h = 0.01;
    float dx = noise(vec2((aPos.x + h) * 3.0 + uTime * 0.8, aPos.z * 3.0)) - 
               noise(vec2((aPos.x - h) * 3.0 + uTime * 0.8, aPos.z * 3.0));
    float dz = noise(vec2(aPos.x * 3.0 + uTime * 0.8, (aPos.z + h) * 3.0)) - 
               noise(vec2(aPos.x * 3.0 + uTime * 0.8, (aPos.z - h) * 3.0));
    
    vNormal = normalize(vec3(-dx * 2.0, 1.0, -dz * 2.0));
    
    gl_Position = uMVP * vec4(displaced, 1.0);
}
)GLSL";

// ─── Water Fragment Shader (ИСПРАВЛЕННЫЙ) ───────────────────────────────────
// ─── Water Fragment Shader (ИСПРАВЛЕННЫЙ ЦВЕТ) ───────────────────────────────────
static const char* FS_WATER = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform float uTime;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform samplerCube uEnvMap;

// Шумовые функции
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

float fresnel(vec3 normal, vec3 viewDir) {
    return pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
}

void main() {
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 normal = normalize(vNormal);
    
    // КРАСИВЫЙ СИНИЙ ЦВЕТ ВОДЫ С ПЕРЕЛИВАНИЕМ
    float depth = max(0.0, 1.0 - length(vWorldPos) * 0.8);
    
    // Основные цвета воды - глубокий синий с изумрудными оттенками
    vec3 shallowColor = vec3(0.1, 0.4, 0.8);   // Бирюзово-синий для мелководья
    vec3 deepColor = vec3(0.0, 0.15, 0.4);     // Глубокий синий
    
    // Добавляем легкие переливания цвета на основе шума
    float colorVariation = fbm(vTexCoord * 0.5 + uTime * 0.1) * 0.2 - 0.1;
    shallowColor.r += colorVariation * 0.1;
    shallowColor.g += colorVariation * 0.2;
    
    vec3 waterColor = mix(deepColor, shallowColor, depth);
    
    // ОТРАЖЕНИЯ С ФРЕНЕЛЕМ
    vec3 reflectDir = reflect(-viewDir, normal);
    vec3 reflection = texture(uEnvMap, reflectDir).rgb;
    
    // Уменьшаем яркость отражений, чтобы не было белого
    reflection *= 0.6;
    
    // Улучшенный френель
    float fresnelFactor = fresnel(normal, viewDir);
    
    // ОСВЕЩЕНИЕ - уменьшаем интенсивность чтобы не было белого
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Диффузное освещение с СИНИМ оттенком
    vec3 diffuse = diff * waterColor * 0.4; // Уменьшено с 0.6
    
    // СПЕКУЛЯРНЫЕ БЛИКИ - СИНИЕ вместо белых
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 128.0); // Более резкие блики
    
    // СИНИЕ блики вместо белых
    vec3 specularColor = vec3(0.3, 0.5, 1.0); // Голубовато-синий
    vec3 specular = spec * specularColor * 0.6; // Уменьшена интенсивность
    
    // ПОДПОВЕРХНОСТНОЕ РАССЕЯНИЕ - СИНЕЕ
    vec3 subsurface = vec3(0.0, 0.15, 0.3) * (1.0 - diff) * 0.2;
    
    // КОМБИНИРОВАНИЕ - сохраняем синий цвет
    vec3 baseColor = waterColor + diffuse + subsurface;
    
    // Смешиваем с отражениями по френелю (меньше отражений)
    vec3 finalColor = mix(baseColor, reflection, fresnelFactor * 0.4); // Уменьшено с 0.7
    
    // Добавляем синие блики
    finalColor += specular;
    
    // ЛЕГКАЯ ТЕКСТУРА ПОВЕРХНОСТИ для переливов
    float surfaceNoise = fbm(vTexCoord * 2.0 + uTime * 0.2) * 0.15 + 0.85;
    finalColor *= surfaceNoise;
    
    // Добавляем легкие цветовые вариации для красоты
    float hueShift = sin(uTime * 0.5 + vTexCoord.x * 3.0) * 0.05;
    finalColor.g += hueShift * 0.1;
    finalColor.b += hueShift * 0.05;
    
    // ПРОЗРАЧНОСТЬ С ГЛУБИНОЙ
    float alpha = 0.8 + depth * 0.15 + fresnelFactor * 0.05;
    
    // Гарантируем, что цвет остается в синих тонах
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    FragColor = vec4(finalColor, alpha);
}
)GLSL";

static const char* VS_MODEL = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_MODEL = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
uniform bool uUseTexture = false;

void main() {
    // Простая затенённая визуализация
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = 0.3 * uColor;
    vec3 diffuse = 0.7 * diff * uColor;
    
    FragColor = vec4(ambient + diffuse, 1.0);
}
)GLSL";

// ─── Жизненный цикл ─────────────────────────────────────────────────────────────
HexSphereWidget::HexSphereWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    // HUD с подсказками
    auto* hud = new QLabel(this);
    hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud->setStyleSheet("QLabel { background: rgba(0,0,0,140); color: white; padding: 6px; }");
    hud->move(10, 10);
    hud->setText("LMB: select | C: clear path | P: build path | +/-: height | 1-8: biomes | S: smooth | W: move");
    hud->adjustSize();

    // Таймер для анимации воды
    waterTimer_ = new QTimer(this);
    connect(waterTimer_, &QTimer::timeout, this, [this]() {
        waterTime_ += 0.016f; // примерно 60 FPS
        update();
        });
}

HexSphereWidget::~HexSphereWidget() {
    makeCurrent();

    if (QOpenGLContext::currentContext()) {
        treeModel_.clearGPUResources();
    }

    if (progWire_)    this->glDeleteProgram(progWire_);
    if (progTerrain_) this->glDeleteProgram(progTerrain_);
    if (progSel_)     this->glDeleteProgram(progSel_);
    if (progWater_)   this->glDeleteProgram(progWater_);
    if (progModel_)   this->glDeleteProgram(progModel_);

    if (vaoWire_)     this->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoTerrain_)  this->glDeleteVertexArrays(1, &vaoTerrain_);
    if (vaoSel_)      this->glDeleteVertexArrays(1, &vaoSel_);
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
    if (waterTimer_) {
        waterTimer_->stop();
    }
    doneCurrent();
}

GLuint HexSphereWidget::makeProgram(const char* vs, const char* fs) {
    GLuint v = this->glCreateShader(GL_VERTEX_SHADER);
    this->glShaderSource(v, 1, &vs, nullptr);
    this->glCompileShader(v);

    // Проверка компиляции вершинного шейдера
    GLint success;
    this->glGetShaderiv(v, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        this->glGetShaderInfoLog(v, 512, nullptr, infoLog);
        qDebug() << "Vertex shader compilation failed:" << infoLog;
    }

    GLuint f = this->glCreateShader(GL_FRAGMENT_SHADER);
    this->glShaderSource(f, 1, &fs, nullptr);
    this->glCompileShader(f);

    // Проверка компиляции фрагментного шейдера
    this->glGetShaderiv(f, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        this->glGetShaderInfoLog(f, 512, nullptr, infoLog);
        qDebug() << "Fragment shader compilation failed:" << infoLog;
    }

    GLuint p = this->glCreateProgram();
    this->glAttachShader(p, v);
    this->glAttachShader(p, f);
    this->glLinkProgram(p);

    // Проверка линковки программы
    this->glGetProgramiv(p, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        this->glGetProgramInfoLog(p, 512, nullptr, infoLog);
        qDebug() << "Shader program linking failed:" << infoLog;
    }

    this->glDeleteShader(v);
    this->glDeleteShader(f);
    return p;
}

void HexSphereWidget::initializeGL() {
    initializeOpenGLFunctions();
    this->glEnable(GL_DEPTH_TEST);
    this->glEnable(GL_CULL_FACE);
    this->glCullFace(GL_BACK);
    this->glFrontFace(GL_CCW);
    updateBufferUsageStrategy();
    // Компилируем шейдеры
    progWire_ = makeProgram(VS_WIRE, FS_WIRE);
    progTerrain_ = makeProgram(VS_TERRAIN, FS_TERRAIN);
    progSel_ = makeProgram(VS_WIRE, FS_SEL);
    progWater_ = makeProgram(VS_WATER, FS_WATER);
    progModel_ = makeProgram(VS_MODEL, FS_MODEL);

    // Получаем uniform locations
    this->glUseProgram(progWire_);    uMVP_Wire_ = this->glGetUniformLocation(progWire_, "uMVP");
    this->glUseProgram(progTerrain_);
    uMVP_Terrain_ = this->glGetUniformLocation(progTerrain_, "uMVP");
    uModel_ = this->glGetUniformLocation(progTerrain_, "uModel");
    uLightDir_ = this->glGetUniformLocation(progTerrain_, "uLightDir");
    this->glUseProgram(progSel_);     uMVP_Sel_ = this->glGetUniformLocation(progSel_, "uMVP");
    // water
    this->glUseProgram(progWater_);
    uMVP_Water_ = this->glGetUniformLocation(progWater_, "uMVP");
    uTime_Water_ = this->glGetUniformLocation(progWater_, "uTime");
    uLightDir_Water_ = this->glGetUniformLocation(progWater_, "uLightDir");
    uViewPos_Water_ = this->glGetUniformLocation(progWater_, "uViewPos");
    uEnvMap_ = this->glGetUniformLocation(progWater_, "uEnvMap");
    generateEnvCubemap();
    this->glUseProgram(0);
    waterTimer_->start(16);
    qDebug() << "Water timer started";
    this->glUseProgram(progModel_);
    uMVP_Model_ = this->glGetUniformLocation(progModel_, "uMVP");
    uModel_Model_ = this->glGetUniformLocation(progModel_, "uModel");
    uLightDir_Model_ = this->glGetUniformLocation(progModel_, "uLightDir");
    uViewPos_Model_ = this->glGetUniformLocation(progModel_, "uViewPos");
    uColor_Model_ = this->glGetUniformLocation(progModel_, "uColor");
    uUseTexture_ = this->glGetUniformLocation(progModel_, "uUseTexture");

    this->glUseProgram(0);

    // Инициализация генератора рельефа по умолчанию - CLIMATE
    setGenerator(std::make_unique<ClimateBiomeTerrainGenerator>());
    setGenParams(TerrainParams{ /*seed=*/12345u, /*seaLevel=*/3, /*scale=*/3.0f });

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
    pyramid.position = getSurfacePoint(0); // Используем правильную начальную позицию
    pyramid.selected = true;
    scene_.addEntity(pyramid);
    initPyramidGeometry();

    // Загружаем модель дерева
    QString modelPath = "Planet/tree.obj";

    if (!treeModel_.loadFromFile(modelPath)) {
        qDebug() << "Failed to load tree model";
    }
    else {
        treeModel_.uploadToGPU();
        qDebug() << "Tree model loaded successfully. Has UVs:" << treeModel_.hasUVs()
            << "Has normals:" << treeModel_.hasNormals()
            << "Is initialized:" << treeModel_.isInitialized();
    }

    glReady_ = true;
    rebuildModel();

    emit hudTextChanged(
        "Controls: [LMB] select | [C] clear path | [P] path between selected | "
        "[+/-] height | [1-8] biomes | [S] smooth toggle | [W] move entity");
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

    stats_.startGPUTimer();
    QVector3D cameraPos = rayOrigin();
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

    // рендеринг воды
    if (waterIndexCount_ > 0 && progWater_) {
        this->glUseProgram(progWater_);

        // Устанавливаем uniform-переменные
        this->glUniformMatrix4fv(uMVP_Water_, 1, GL_FALSE, mvp.constData());

        // передаём время для анимации
        this->glUniform1f(uTime_Water_, waterTime_);

        // Передаем параметры освещения и камеры
        this->glUniform3f(uLightDir_Water_, lightDir_.x(), lightDir_.y(), lightDir_.z());
        this->glUniform3f(uViewPos_Water_, cameraPos.x(), cameraPos.y(), cameraPos.z());

        // Активируем кубическую карту (если используешь расширенную версию)
        if (uEnvMap_ != -1 && envCubemap_ != 0) {
            this->glActiveTexture(GL_TEXTURE0);
            this->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);
            this->glUniform1i(uEnvMap_, 0);
        }

        // Включаем blending для прозрачности
        this->glEnable(GL_BLEND);
        this->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Отключаем запись в буфер глубины для воды
        this->glDepthMask(GL_FALSE);

        // Рендерим всю воду одним вызовом
        this->glBindVertexArray(vaoWater_);
        this->glDrawElements(GL_TRIANGLES, waterIndexCount_, GL_UNSIGNED_INT, nullptr);
        this->glBindVertexArray(0);

        // Восстанавливаем настройки
        this->glDepthMask(GL_TRUE);
        this->glDisable(GL_BLEND);

        // qDebug() << "Water rendered:" << waterIndexCount_ << "indices, time:" << currentTime;
    }

    // Рендеринг объектов сцены (пирамиды)
    // Рендеринг объектов сцены (пирамиды)
    for (const auto& e : scene_.entities()) {
        // Получаем позицию на поверхности с правильной высотой
        QVector3D surfacePos = getSurfacePoint(e.currentCell);

        QMatrix4x4 model;
        model.translate(surfacePos); // Используем surfacePos вместо e.position

        // Применяем ориентацию к нормали поверхности
        QVector3D surfaceNormal = surfacePos.normalized();
        orientToSurfaceNormal(model, surfaceNormal);

        model.scale(0.08f); // Увеличенный размер

        // Если объект выделен, увеличиваем его немного
        if (e.selected) {
            model.scale(1.2f);
        }

        const QMatrix4x4 mvp = proj_ * view_ * model;

        // Используем разные шейдеры для выделенных объектов
        if (e.selected) {
            this->glUseProgram(progSel_); // желтый цвет для выделения
            this->glUniformMatrix4fv(uMVP_Sel_, 1, GL_FALSE, mvp.constData());
        }
        else {
            this->glUseProgram(progWire_);
            this->glUniformMatrix4fv(uMVP_Wire_, 1, GL_FALSE, mvp.constData());
        }

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

    if (uEnvMap_ != -1 && envCubemap_ != 0) {
        this->glActiveTexture(GL_TEXTURE0);
        this->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);
        this->glUniform1i(uEnvMap_, 0);
    }

    // Рендеринг деревьев
    if (treeModel_.isInitialized() && progModel_ != 0 && !treeModel_.isEmpty()) {
        this->glUseProgram(progModel_);

        QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
        QVector3D eye = (view_.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

        this->glUniform3f(uLightDir_Model_, globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
        this->glUniform3f(uViewPos_Model_, eye.x(), eye.y(), eye.z());
        this->glUniform3f(uColor_Model_, 0.15f, 0.5f, 0.1f); // Зелёный цвет для деревьев
        this->glUniform1i(uUseTexture_, treeModel_.hasUVs() ? 1 : 0);

        const auto& cells = model_.cells();
        int treesRendered = 0;
        const int maxTrees = 25; // Ограничим количество для производительности

        for (size_t i = 0; i < cells.size() && treesRendered < maxTrees; ++i) {
            if (cells[i].biome == Biome::Grass && (i % 3 == 0)) { // Каждое третье дерево на траве
                QVector3D treePos = getSurfacePoint((int)i);

                QMatrix4x4 model;
                model.translate(treePos);
                orientToSurfaceNormal(model, treePos.normalized());
                model.scale(0.05f + 0.02f * (i % 5)); // Немного варьируем размер

                QMatrix4x4 mvp = proj_ * view_ * model;

                this->glUniformMatrix4fv(uMVP_Model_, 1, GL_FALSE, mvp.constData());
                this->glUniformMatrix4fv(uModel_Model_, 1, GL_FALSE, model.constData());

                treeModel_.draw(progModel_, mvp, model, view_);
                treesRendered++;
            }
        }
    }

    // Завершаем замер времени рендеринга
    stats_.stopGPUTimer();

    // Обновляем счетчик кадров
    stats_.frameRendered();

    // Выводим статистику в debug
    static int frameCounter = 0;
    if (frameCounter++ % 60 == 0) { // Каждые ~60 кадров
        qDebug() << stats_.getStatsString();

        // Детальная статистика при высоких нагрузках
        if (L_ >= 4) {
            qDebug() << stats_.getDetailedStats();
        }
    }
}

void HexSphereWidget::orientToSurfaceNormal(QMatrix4x4& matrix, const QVector3D& normal) {
    QVector3D up = normal.normalized();

    // Выбираем произвольный вектор, не параллельный нормали
    QVector3D forward = (qAbs(QVector3D::dotProduct(up, QVector3D(0, 0, 1))) > 0.99f)
        ? QVector3D(1, 0, 0)
        : QVector3D(0, 0, 1);

    // Создаем ортонормированный базис
    QVector3D right = QVector3D::crossProduct(forward, up).normalized();
    forward = QVector3D::crossProduct(up, right).normalized();

    // Создаем матрицу вращения
    QMatrix4x4 rotation;
    rotation.setColumn(0, QVector4D(right, 0.0f));
    rotation.setColumn(1, QVector4D(up, 0.0f));
    rotation.setColumn(2, QVector4D(forward, 0.0f));
    rotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));

    matrix = matrix * rotation;
}

QVector3D HexSphereWidget::getSurfacePoint(int cellId) const {
    const auto& cells = model_.cells();
    if (cellId < 0 || cellId >= (int)cells.size())
        return QVector3D(0, 0, 1.0f);

    const Cell& cell = cells[(size_t)cellId];

    // Учитываем высоту ячейки и добавляем небольшой отступ
    float surfaceHeight = 1.0f + cell.height * heightStep_;

    // ДОБАВЛЯЕМ дополнительный отступ для объектов, чтобы они не проваливались
    float objectOffset = 0.03f; // Отступ над поверхностью

    return cell.centroid.normalized() * (surfaceHeight + objectOffset);
}

// ─── Build/Upload ─────────────────────────────────────────────────────────────
void HexSphereWidget::rebuildModel() {
    // Обновляем стратегию буферов перед перестройкой
    updateBufferUsageStrategy();

    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);

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
    std::vector<float> lineVerts;
    lineVerts.reserve(model_.wireEdges().size() * 6);

    for (auto [a, b] : model_.wireEdges()) {
        const auto& pa = V[size_t(a)];
        const auto& pb = V[size_t(b)];
        lineVerts.insert(lineVerts.end(), { pa.x(),pa.y(),pa.z(), pb.x(),pb.y(),pb.z() });
    }

    this->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    this->glBufferData(GL_ARRAY_BUFFER,
        GLsizeiptr(lineVerts.size() * sizeof(float)),
        lineVerts.data(),
        wireBufferUsage_);

    lineVertexCount_ = GLsizei(lineVerts.size() / 3);

    // Обновляем статистику
    stats_.updateMemoryStats(lineVertexCount_, 0, 0);

    doneCurrent();
}

void HexSphereWidget::uploadTerrainBuffers() {
    if (!glReady_) { gpuDirty_ = true; return; }
    makeCurrent();

    // НАЧАЛО ЗАМЕРА ВРЕМЕНИ
    stats_.startGPUTimer();

    TerrainTessellator tt;
    tt.R = 1.0f;
    heightStep_ = autoHeightStep();
    tt.heightStep = heightStep_;
    tt.inset = stripInset_;
    tt.smoothMaxDelta = smoothOneStep_ ? 1 : 0;
    tt.outerTrim = 0.15f;
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

    // Позиции - ВОЗВРАЩАЕМ ОРИГИНАЛЬНУЮ ЛОГИКУ
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    this->glBufferData(GL_ARRAY_BUFFER, vbPos, terrainCPU_.pos.empty() ? nullptr : terrainCPU_.pos.data(), GL_DYNAMIC_DRAW);

    // Цвета
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    this->glBufferData(GL_ARRAY_BUFFER, vbCol, terrainCPU_.col.empty() ? nullptr : terrainCPU_.col.data(), GL_DYNAMIC_DRAW);

    // Нормали
    this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    this->glBufferData(GL_ARRAY_BUFFER, vbNorm, terrainCPU_.norm.empty() ? nullptr : terrainCPU_.norm.data(), GL_DYNAMIC_DRAW);

    // Индексы
    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    this->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, terrainCPU_.idx.empty() ? nullptr : terrainCPU_.idx.data(), GL_DYNAMIC_DRAW);

    this->glBindBuffer(GL_ARRAY_BUFFER, 0);
    terrainIndexCount_ = GLsizei(terrainCPU_.idx.size());

    // ОБНОВЛЯЕМ СТАТИСТИКУ
    stats_.updateMemoryStats(
        GLsizei(terrainCPU_.pos.size() / 3),
        terrainIndexCount_,
        terrainIndexCount_ / 3
    );

    // КОНЕЦ ЗАМЕРА ВРЕМЕНИ
    stats_.stopGPUTimer();

    // Создаем геометрию воды
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
    std::vector<float> waterEdgeFlags;
    std::vector<uint32_t> waterIndices;

    // ВЫСОТА ДНА для всех морских ячеек - УБИРАЕМ ДНО!
    const float SEA_LEVEL = 1.0f;

    // Уровень детализации воды - подразделяем ячейки для плавных волн
    const unsigned int WATER_SUBDIVISIONS = 3;

    // Объявляем лямбду заранее и используем std::function для рекурсии
    std::function<void(const QVector3D&, const QVector3D&, const QVector3D&,
        float, float, float, unsigned int)> subdivideTriangle;

    subdivideTriangle = [&](const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
        float edge0, float edge1, float edge2, unsigned int level) {
            if (level <= 0) {
                // Базовый случай - добавляем треугольник
                unsigned int i0 = static_cast<unsigned int>(waterPos.size() / 3);
                waterPos.insert(waterPos.end(), { v0.x(), v0.y(), v0.z() });
                waterEdgeFlags.push_back(edge0);

                unsigned int i1 = static_cast<unsigned int>(waterPos.size() / 3);
                waterPos.insert(waterPos.end(), { v1.x(), v1.y(), v1.z() });
                waterEdgeFlags.push_back(edge1);

                unsigned int i2 = static_cast<unsigned int>(waterPos.size() / 3);
                waterPos.insert(waterPos.end(), { v2.x(), v2.y(), v2.z() });
                waterEdgeFlags.push_back(edge2);

                waterIndices.insert(waterIndices.end(), { i0, i1, i2 });
                return;
            }

            // Рекурсивное подразделение
            QVector3D mid01 = (v0 + v1) * 0.5f;
            QVector3D mid12 = (v1 + v2) * 0.5f;
            QVector3D mid20 = (v2 + v0) * 0.5f;

            // Нормализуем средние точки к сфере
            mid01 = mid01.normalized() * SEA_LEVEL;
            mid12 = mid12.normalized() * SEA_LEVEL;
            mid20 = mid20.normalized() * SEA_LEVEL;

            // Средние значения флагов краев
            float edge_mid01 = (edge0 + edge1) * 0.5f;
            float edge_mid12 = (edge1 + edge2) * 0.5f;
            float edge_mid20 = (edge2 + edge0) * 0.5f;

            // Рекурсивно подразделяем 4 новых треугольника
            subdivideTriangle(v0, mid01, mid20, edge0, edge_mid01, edge_mid20, level - 1);
            subdivideTriangle(mid01, v1, mid12, edge_mid01, edge1, edge_mid12, level - 1);
            subdivideTriangle(mid20, mid12, v2, edge_mid20, edge_mid12, edge2, level - 1);
            subdivideTriangle(mid01, mid12, mid20, edge_mid01, edge_mid12, edge_mid20, level - 1);
        };

    // Создаем воду ТОЛЬКО ДЛЯ ПОВЕРХНОСТИ (без дна)
    for (size_t cellIdx = 0; cellIdx < cells.size(); ++cellIdx) {
        const auto& cell = cells[cellIdx];
        if (cell.biome != Biome::Sea) continue;
        if (cell.poly.size() < 3) continue;

        // Центр ячейки на уровне моря - ЭТО НЕ КРАЙ (flag = 0.0)
        QVector3D center = cell.centroid.normalized() * SEA_LEVEL;

        // Вершины по краям ячейки - ЭТО КРАЯ (flag = 1.0)
        std::vector<QVector3D> vertices;
        std::vector<float> vertexEdgeFlags;

        for (int dv : cell.poly) {
            const QVector3D& vert = dual[static_cast<size_t>(dv)];
            QVector3D waterVert = vert.normalized() * SEA_LEVEL;
            vertices.push_back(waterVert);
            vertexEdgeFlags.push_back(1.0f); // Край ячейки
        }

        // Создаем треугольники веером от центра с ПОДРАЗДЕЛЕНИЕМ
        size_t numVertices = vertices.size();
        for (size_t i = 0; i < numVertices; ++i) {
            size_t next_i = (i + 1) % numVertices;

            // Подразделяем каждый треугольник для плавных волн
            subdivideTriangle(
                center,                    // центр - не край (0.0)
                vertices[i],               // вершина - край (1.0)  
                vertices[next_i],          // следующая вершина - край (1.0)
                0.0f,                      // центр - не край
                vertexEdgeFlags[i],        // вершина - край
                vertexEdgeFlags[next_i],   // следующая вершина - край
                WATER_SUBDIVISIONS
            );
        }
    }

    // Загружаем данные в буферы
    this->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    this->glBufferData(GL_ARRAY_BUFFER,
        waterPos.size() * sizeof(float),
        waterPos.empty() ? nullptr : waterPos.data(),
        GL_STATIC_DRAW);

    // Создаем отдельный буфер для флагов краев
    GLuint vboWaterEdgeFlags;
    this->glGenBuffers(1, &vboWaterEdgeFlags);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags);
    this->glBufferData(GL_ARRAY_BUFFER,
        waterEdgeFlags.size() * sizeof(float),
        waterEdgeFlags.empty() ? nullptr : waterEdgeFlags.data(),
        GL_STATIC_DRAW);

    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    this->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        waterIndices.size() * sizeof(uint32_t),
        waterIndices.empty() ? nullptr : waterIndices.data(),
        GL_STATIC_DRAW);

    // Обновляем VAO для воды с новым атрибутом
    this->glBindVertexArray(vaoWater_);
    this->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(0);

    this->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags);
    this->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    this->glEnableVertexAttribArray(1);

    this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    this->glBindVertexArray(0);

    waterIndexCount_ = static_cast<GLsizei>(waterIndices.size());

    qDebug() << "=== IMPROVED WATER GEOMETRY ===";
    qDebug() << "Water vertices:" << waterPos.size() / 3;
    qDebug() << "Water triangles:" << waterIndexCount_ / 3;
    qDebug() << "Subdivision level:" << WATER_SUBDIVISIONS;
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

    // Статичная камера смотрит на центр
    const QVector3D eye = QVector3D(0, 0, distance_);
    const QVector3D center = QVector3D(0, 0, 0);
    const QVector3D up = QVector3D(0, 1, 0);

    view_.lookAt(eye, center, up);

    // Вращение применяется ко всей сцене (планете)
    view_.rotate(sphereRotation_);
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

    if (e->button() == Qt::RightButton) {
        rotating_ = true;
    }
    else if (e->button() == Qt::LeftButton) {
        if (!glReady_) return;

        auto hit = pickSceneAt(e->pos().x(), e->pos().y());
        if (hit) {
            if (hit->isEntity) {
                // Клик по объекту - выделяем его
                selectEntity(hit->entityId);
                qDebug() << "Selected entity:" << hit->entityId;
            }
            else if (selectedEntityId_ != -1) {
                // Клик по ландшафту при выделенном объекте - перемещаем объект
                moveSelectedEntityToCell(hit->cellId);
                // Снимаем выделение после перемещения (опционально)
                deselectEntity();
            }
            else {
                // Клик по ландшафту без выделенного объекта - работа с ячейками
                int cid = hit->cellId;
                if (selectedCells_.contains(cid)) {
                    selectedCells_.remove(cid);
                }
                else {
                    selectedCells_.insert(cid);
                }
                uploadSelectionOutlineBuffers();
            }
            update();
        }
        else {
            // Клик в пустоту - снимаем все выделения
            deselectEntity();
            update();
        }
    }
}

void HexSphereWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!rotating_) return;

    const QPoint currentPos = e->pos();
    const QPoint delta = currentPos - lastPos_;
    lastPos_ = currentPos;

    if (delta.manhattanLength() == 0) return;

    // ИЗМЕНЕНИЕ: поменяли знаки на противоположные
    const float sensitivity = 0.002f;

    // Вращение вокруг осей X и Y в зависимости от движения мыши
    QQuaternion rotationX = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), delta.x() * sensitivity * 180.0f);  // убрали минус
    QQuaternion rotationY = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), delta.y() * sensitivity * 180.0f);  // убрали минус

    // Комбинируем вращения
    QQuaternion rotation = rotationY * rotationX;

    // Применяем вращение к текущей ориентации планеты
    sphereRotation_ = rotation * sphereRotation_;

    update();
}

void HexSphereWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        rotating_ = false;
    }
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

        // ИСПРАВЛЕНО: используем getSurfacePoint для правильного позиционирования
        ent.position = getSurfacePoint(next);

        update();
        break; // ДОБАВЬ break чтобы не проваливаться в следующий case!
    }
    case Qt::Key_Escape:
        // ESC - снять выделение
        deselectEntity();
        update();
        return;
    case Qt::Key_Delete:
        // Delete - удалить выделенный объект
        if (selectedEntityId_ != -1) {
            scene_.removeEntity(selectedEntityId_);
            selectedEntityId_ = -1;
            update();
        }
        return;
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
    case Qt::Key_4:
        apply([&](int cid) { model_.setBiome(cid, Biome::Snow);  });
        break;
    case Qt::Key_5:
        apply([&](int cid) { model_.setBiome(cid, Biome::Tundra); });
        break;
    case Qt::Key_6:
        apply([&](int cid) { model_.setBiome(cid, Biome::Desert); });
        break;
    case Qt::Key_7:
        apply([&](int cid) { model_.setBiome(cid, Biome::Savanna); });
        break;
    case Qt::Key_8:
        apply([&](int cid) { model_.setBiome(cid, Biome::Jungle); });
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
        stats_.setSubdivisionLevel(L); // Обновляем уровень в статистике

        // Обновляем стратегию использования буферов
        updateBufferUsageStrategy();

        heightStep_ = autoHeightStep();
        rebuildModel();
        update();
    }
}

void HexSphereWidget::resetView() {
    distance_ = 2.2f;
    sphereRotation_ = QQuaternion(); // сбрасываем вращение
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

void HexSphereWidget::setGeneratorByIndex(int idx) {
    switch (idx) {
    case 0: setGenerator(std::make_unique<NoOpTerrainGenerator>()); break;
    case 1: setGenerator(std::make_unique<SineTerrainGenerator>()); break;
    case 2: setGenerator(std::make_unique<PerlinTerrainGenerator>()); break;
    case 3: setGenerator(std::make_unique<ClimateBiomeTerrainGenerator>()); break;
    default: setGenerator(std::make_unique<ClimateBiomeTerrainGenerator>()); break;
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
    if (bestOwner >= 0) {
        // ИСПРАВЛЕНО: создаем PickHit с правильными параметрами
        return PickHit{ bestOwner, -1, bestPos, bestT, false };
    }
    return std::nullopt;
}

std::optional<HexSphereWidget::PickHit> HexSphereWidget::pickEntityAt(int sx, int sy) const {
    const QVector3D ro = rayOrigin();
    const QVector3D rd = rayDirectionFromScreen(sx, sy);

    float bestT = std::numeric_limits<float>::infinity();
    int bestEntityId = -1;
    QVector3D bestPos;

    // Проверяем все объекты сцены
    for (const auto& entity : scene_.entities()) {
        // ИСПРАВЛЕНО: используем getSurfacePoint для правильной позиции
        QVector3D surfacePos = getSurfacePoint(entity.currentCell);

        // Простая проверка пересечения с bounding sphere объекта
        float radius = 0.08f; // Увеличили радиус для большей пирамиды
        QVector3D center = surfacePos;

        // Проверка пересечения луча со сферой
        QVector3D oc = ro - center;
        float a = QVector3D::dotProduct(rd, rd);
        float b = 2.0f * QVector3D::dotProduct(oc, rd);
        float c = QVector3D::dotProduct(oc, oc) - radius * radius;
        float discriminant = b * b - 4 * a * c;

        if (discriminant > 0) {
            float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
            if (t > 0 && t < bestT) {
                bestT = t;
                bestEntityId = entity.id;
                bestPos = ro + rd * t;
            }
        }
    }

    if (bestEntityId >= 0) {
        return PickHit{ -1, bestEntityId, bestPos, bestT, true };
    }
    return std::nullopt;
}

std::optional<HexSphereWidget::PickHit> HexSphereWidget::pickSceneAt(int sx, int sy) const {
    // Сначала проверяем объекты
    auto entityHit = pickEntityAt(sx, sy);
    if (entityHit) {
        return entityHit;
    }

    // Если не попали в объект, проверяем ландшафт
    auto terrainHit = pickTerrainAt(sx, sy);
    if (terrainHit) {
        return terrainHit;
    }

    return std::nullopt;
}

void HexSphereWidget::selectEntity(int entityId) {
    // Снимаем выделение с предыдущего объекта
    deselectEntity();

    // Устанавливаем выделение новому объекту
    selectedEntityId_ = entityId;
    auto entityOpt = scene_.getEntity(entityId);
    if (entityOpt) {
        SceneEntity& entity = entityOpt->get();
        entity.selected = true;
    }

    // Обновляем отрисовку
    update();
}

void HexSphereWidget::deselectEntity() {
    if (selectedEntityId_ != -1) {
        auto entityOpt = scene_.getEntity(selectedEntityId_);
        if (entityOpt) {
            SceneEntity& entity = entityOpt->get();
            entity.selected = false;
        }
        selectedEntityId_ = -1;
    }
}

void HexSphereWidget::moveSelectedEntityToCell(int cellId) {
    if (selectedEntityId_ == -1) return;

    auto entityOpt = scene_.getEntity(selectedEntityId_);
    if (!entityOpt) return;

    SceneEntity& entity = entityOpt->get();

    // Обновляем позицию и привязку к ячейке
    if (cellId >= 0 && cellId < model_.cellCount()) {
        const auto& cell = model_.cells()[cellId];
        entity.currentCell = cellId;

        // ИСПРАВЛЕНО: используем getSurfacePoint для правильного позиционирования
        entity.position = getSurfacePoint(cellId);

        qDebug() << "Moved entity" << selectedEntityId_ << "to cell" << cellId
            << "at height:" << cell.height;
    }

    update();
}

void HexSphereWidget::generateEnvCubemap() {
    if (envCubemap_) return;

    this->glGenTextures(1, &envCubemap_);
    this->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

    // Создаем простую кубическую карту (можно загрузить реальные текстуры)
    const int size = 512;
    for (unsigned int i = 0; i < 6; ++i) {
        std::vector<unsigned char> data(size * size * 3);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int idx = (y * size + x) * 3;
                // Простой градиент неба
                data[idx] = 100 + (y * 155 / size);     // R
                data[idx + 1] = 150 + (y * 105 / size); // G  
                data[idx + 2] = 255;                    // B
            }
        }
        this->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
            size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    }

    this->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    this->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    this->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    this->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    this->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void HexSphereWidget::updateBufferUsageStrategy() {
    // Для высоких уровней детализации используем динамические буферы
    if (L_ >= 4) {
        terrainBufferUsage_ = GL_DYNAMIC_DRAW;
        wireBufferUsage_ = GL_DYNAMIC_DRAW;
        useStaticBuffers_ = false;
    }
    else {
        terrainBufferUsage_ = GL_STATIC_DRAW;
        wireBufferUsage_ = GL_STATIC_DRAW;
        useStaticBuffers_ = true;
    }

    qDebug() << "Buffer strategy:" << (useStaticBuffers_ ? "STATIC" : "DYNAMIC")
        << "for L =" << L_;
}

void HexSphereWidget::optimizeTerrainBuffers() {
    if (!glReady_) return;

    makeCurrent();

    // Оптимизация: переиспользование буферов вместо пересоздания
    if (useStaticBuffers_) {
        // Статические буферы - полная перезапись
        this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
        this->glBufferData(GL_ARRAY_BUFFER,
            terrainCPU_.pos.size() * sizeof(float),
            terrainCPU_.pos.data(),
            terrainBufferUsage_);

        this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
        this->glBufferData(GL_ARRAY_BUFFER,
            terrainCPU_.col.size() * sizeof(float),
            terrainCPU_.col.data(),
            terrainBufferUsage_);
    }
    else {
        // Динамические буферы - обновление существующих данных
        // Используем glBufferSubData для частичного обновления когда возможно
        this->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
        this->glBufferData(GL_ARRAY_BUFFER,
            terrainCPU_.pos.size() * sizeof(float),
            terrainCPU_.pos.data(),
            terrainBufferUsage_);
    }

    doneCurrent();
}