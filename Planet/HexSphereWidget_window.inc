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

    if (QOpenGLContext::currentContext() && treeModel_.use_count() == 1) {
        treeModel_->clearGPUResources();
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
    SceneEntity pyramid("Explorer", "pyramid");
    pyramid.setCurrentCell(0);
    QVector3D surfacePosition = getSurfacePoint(0);
    pyramid.transform().position = scene::localToWorldPoint(pyramid.transform(), globalFrame_, surfacePosition);
    pyramid.attachCollider(std::make_unique<scene::SphereCollider>(0.08f));
    pyramid.setSelected(true);
    scene_.addEntity(pyramid);
    initPyramidGeometry();

    // Загружаем модель дерева
    QString modelPath = "Planet/tree.obj";

    treeModel_ = ModelHandler::loadShared(modelPath);
    if (!treeModel_) {
        qDebug() << "Failed to load tree model";
    }
    else {
        treeModel_->uploadToGPU();
        qDebug() << "Tree model loaded successfully. Has UVs:" << treeModel_->hasUVs()
            << "Has normals:" << treeModel_->hasNormals()
            << "Is initialized:" << treeModel_->isInitialized();
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
        QVector3D surfacePos = getSurfacePoint(e->currentCell());

        QMatrix4x4 model;
        model.translate(surfacePos); // Используем surfacePos вместо e.position

        // Применяем ориентацию к нормали поверхности
        QVector3D surfaceNormal = surfacePos.normalized();
        orientToSurfaceNormal(model, surfaceNormal);

        model.scale(0.08f); // Увеличенный размер

        // Если объект выделен, увеличиваем его немного
        if (e->selected()) {
            model.scale(1.2f);
        }

        const QMatrix4x4 mvp = proj_ * view_ * model;

        // Используем разные шейдеры для выделенных объектов
        if (e->selected()) {
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
    if (treeModel_ && treeModel_->isInitialized() && progModel_ != 0 && !treeModel_->isEmpty()) {
        this->glUseProgram(progModel_);

        QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
        QVector3D eye = (view_.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

        this->glUniform3f(uLightDir_Model_, globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
        this->glUniform3f(uViewPos_Model_, eye.x(), eye.y(), eye.z());
        this->glUniform3f(uColor_Model_, 0.15f, 0.5f, 0.1f); // Зелёный цвет для деревьев
        this->glUniform1i(uUseTexture_, treeModel_->hasUVs() ? 1 : 0);

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

                treeModel_->draw(progModel_, mvp, model, view_);
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

