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
