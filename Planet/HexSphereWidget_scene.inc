
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
        if (ent.currentCell() < 0 || ent.currentCell() >= (int)cells.size()) return;
        const auto& c = cells[(size_t)ent.currentCell()];
        if (c.neighbors.empty()) return;
        int next = c.neighbors[0]; // просто первый сосед
        if (next < 0) return;
        ent.setCurrentCell(next);

        // ИСПРАВЛЕНО: используем getSurfacePoint для правильного позиционирования
        ent.transform().position = getSurfacePoint(next);

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
        QVector3D surfacePos = getSurfacePoint(entity->currentCell());

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
                bestEntityId = entity->id();
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
        entity.setSelected(true);
    }

    // Обновляем отрисовку
    update();
}

void HexSphereWidget::deselectEntity() {
    if (selectedEntityId_ != -1) {
        auto entityOpt = scene_.getEntity(selectedEntityId_);
        if (entityOpt) {
            SceneEntity& entity = entityOpt->get();
            entity.setSelected(false);
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
        entity.setCurrentCell(cellId);

        // ИСПРАВЛЕНО: используем getSurfacePoint для правильного позиционирования
        entity.transform().position = getSurfacePoint(cellId);

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