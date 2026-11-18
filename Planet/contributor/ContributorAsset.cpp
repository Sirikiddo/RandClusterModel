#include "contributor/ContributorAsset.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <QDebug>
#include <random>
#include <thread>
#include <vector>

#include "contributor/ContributorParticles.h"
#include "contributor/TreeBuilder.h"
#include "core/AppViewConfig.h"

namespace {

    constexpr float kPi = 3.14159265358979f;

    // ================ Конвертация simple3d → ассет ================
    // (оставляем только то, что реально нужно для ContributorAsset)

    // Базовый мировой масштаб дерева. Это единственный абсолютный множитель:
    // масштаб топологии планеты и мелкий сценарный разброс рендерер добавляет сам.
    constexpr float kGlobalScale = 0.12f;

    // Эталонная локальная высота дерева. К ней приводится каждый вариант, поэтому
    // ни один вид не может оказаться деревом-гигантом.
    constexpr float kReferenceLocalTreeHeight = 2.0f;

    // Максимальный радиус облака кроны в локальных единицах. Совпадает с верхней
    // границей distRadiusLarge(...) в TreeBuilder::generateParticles().
    constexpr float kMaxLeafBlobRadius = 0.65f;

    // Узкая полоса видового характера размера. Значения
    // TreeBuilder::realisticScaleForSpecies() различаются вчетверо (0.4..1.6) и
    // рассчитаны на старую схему, где высота вида задавалась ещё и параметрами
    // меша. Складывать видовой коэффициент с высотой вида нельзя: при пересборке
    // сцены (например, после смены SubdivisionLevel) такое дерево вырастало в разы
    // сильнее остальных.
    constexpr float kMinSpeciesScale = 0.92f;
    constexpr float kMaxSpeciesScale = 1.15f;

    // Полная высота дерева в локальных координатах меша: ствол с ветками плюс
    // облако кроны над ними.
    float localTreeHeight(const TreeBuilder::TreeMeshData& mesh) {
        float height = 0.0f;
        for (size_t i = 1; i + 2 < mesh.wood.positions.size(); i += 3) {
            height = std::max(height, mesh.wood.positions[i]);
        }
        for (const QVector3D& tip : mesh.branchTips) {
            height = std::max(height, tip.y() + kMaxLeafBlobRadius);
        }
        return height;
    }

    // Единый множитель размера варианта: узкий видовой характер, приведённый к
    // эталонной локальной высоте дерева. Именно это значение рендерер умножает на
    // топологический масштаб планеты, поэтому мировой размер дерева определяется
    // SubdivisionLevel, а не случайным пересбором сцены.
    float treeScaleForVariant(const TreeBuilder::TreeVariant& variant, float localHeight) {
        const float speciesScale = std::clamp(
            TreeBuilder::realisticScaleForSpecies(variant.species),
            kMinSpeciesScale,
            kMaxSpeciesScale);
        if (localHeight <= 1e-3f) {
            return speciesScale;
        }
        return speciesScale * (kReferenceLocalTreeHeight / localHeight);
    }

    void appendGeneratedStylizedTreeMeshes(ContributorAsset& asset) {
        constexpr int kVariantsPerSpecies = 8;

        auto cache = TreeBuilder::generateCache(kVariantsPerSpecies);

        asset.speciesMeshes.resize(TreeBuilder::kTreeSpeciesCount);
        asset.speciesParticles.resize(TreeBuilder::kTreeSpeciesCount);

        const size_t totalVariants = static_cast<size_t>(TreeBuilder::kTreeSpeciesCount)
            * static_cast<size_t>(kVariantsPerSpecies);

        struct VariantBuild {
            simple3d::Mesh wood;
            std::vector<ContributorParticle> particles;
            float realisticScale = 1.0f;
        };
        std::vector<VariantBuild> builds(totalVariants);

        // Варианты независимы: каждый использует собственный seed и локальный
        // генератор. Деревья собираются на нескольких потоках, а порядок
        // результатов сохраняется по индексу (species, variant).
        std::atomic<size_t> nextIndex{ 0 };
        const unsigned workerCount = std::max(1u, std::min(
            8u, std::thread::hardware_concurrency()));
        auto worker = [&]() {
            for (;;) {
                const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (index >= totalVariants) {
                    return;
                }
                const int s = static_cast<int>(index / kVariantsPerSpecies);
                const int v = static_cast<int>(index % kVariantsPerSpecies);

                const TreeBuilder::TreeVariant& variant = cache[static_cast<size_t>(s)][static_cast<size_t>(v)];
                auto meshData = TreeBuilder::generateMesh(variant.params);

                VariantBuild build;
                // Видовой коэффициент становится полным множителем размера
                // варианта: видовой характер × нормировка высоты меша.
                build.realisticScale = treeScaleForVariant(
                    variant, localTreeHeight(meshData));
                build.particles = TreeBuilder::generateParticles(
                    meshData, variant, kGlobalScale);
                build.wood = std::move(meshData.wood);
                builds[index] = std::move(build);
            }
            };

        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (unsigned i = 0; i < workerCount; ++i) {
            workers.emplace_back(worker);
        }
        for (std::thread& thread : workers) {
            thread.join();
        }

        for (int s = 0; s < TreeBuilder::kTreeSpeciesCount; ++s) {
            asset.speciesMeshes[s].resize(kVariantsPerSpecies);
            asset.speciesParticles[s].resize(kVariantsPerSpecies);
            for (int v = 0; v < kVariantsPerSpecies; ++v) {
                VariantBuild& build = builds[static_cast<size_t>(s) * kVariantsPerSpecies + v];
                cache[static_cast<size_t>(s)][static_cast<size_t>(v)].realisticScale =
                    build.realisticScale;
                asset.speciesMeshes[s][v] = std::move(build.wood);
                asset.speciesParticles[s][v] = std::move(build.particles);
            }

            qDebug() << "Species" << s << "generated"
                << kVariantsPerSpecies << "variants,"
                << asset.speciesParticles[s][0].size() << "particles/variant (first)";
        }

        // Метаданные вариантов нужны рендереру вместе с пересчитанным
        // realisticScale, поэтому публикуем кэш уже после обработки всех вариантов.
        asset.speciesVariants = std::move(cache);

        qDebug() << "Total species:" << asset.speciesMeshes.size()
            << "variants per species:" << kVariantsPerSpecies;
    }

    // =====================================================================
    // ПЕСОЧНИЦА: ОДИН ШЕСТИУГОЛЬНИК
    // =====================================================================
    // Плитка лежит в плоскости XY центром в начале координат, нормаль +Z:
    // камера песочницы стоит в (0, 0, distance_) и смотрит в начало координат,
    // поэтому "лицом" к ней должна быть сторона +Z.
    //
    // Сетка полярная: rings колец от центра к границе, на каждом кольце
    // 6 * segmentsPerSide вершин. Вершины ставятся не по окружности, а по
    // границе шестиугольника (пересечение луча с шестью рёбрами), поэтому углы
    // остаются острыми, а не срезанными окружностью.
    //
    // UV — плоская проекция плитки в 0..1 по её габаритам, так что тайлинг
    // текстуры дальше делается умножением в шейдере, без пересборки меша.
    //
    // Сейчас плитка идеально плоская: все нормали (0, 0, 1). Рельеф, шум и
    // нормал-маппинг — следующий шаг, плотности сетки под него уже достаточно.
    // =====================================================================

    struct HexSandboxParams {
        float radius = 1.0f;       // радиус описанной окружности (центр -> угол)
        int rings = 8;             // колец от центра к границе
        int segmentsPerSide = 6;   // точек на одну сторону; чётное => углы ложатся в вершины
    };

    // Радиус границы правильного шестиугольника в направлении angle:
    // фигура = пересечение 6 полуплоскостей n_i * p <= apothem.
    float hexBoundaryRadius(float angle, float radius) {
        const float apothem = radius * std::cos(kPi / 6.0f);
        const float dx = std::cos(angle);
        const float dy = std::sin(angle);

        float best = radius * 2.0f;
        for (int i = 0; i < 6; ++i) {
            const float a = float(i) * kPi / 3.0f;
            const float denom = std::cos(a) * dx + std::sin(a) * dy;
            if (denom > 1e-6f) {
                best = std::min(best, apothem / denom);
            }
        }
        return best;
    }

    simple3d::Mesh buildHexSandboxMesh(const HexSandboxParams& params) {
        simple3d::Mesh mesh;

        const int ringCount = std::max(params.rings, 1);
        int segmentsPerSide = std::max(params.segmentsPerSide, 2);
        if (segmentsPerSide % 2 != 0) {
            ++segmentsPerSide;
        }
        const int perRing = 6 * segmentsPerSide;

        const float radius = std::max(params.radius, 1e-4f);
        const float invRadius = 1.0f / radius;

        const size_t vertexCount = size_t(1 + ringCount * perRing);
        mesh.positions.reserve(vertexCount * 3);
        mesh.normals.reserve(vertexCount * 3);
        mesh.texcoords.reserve(vertexCount * 2);
        mesh.indices.reserve(size_t(perRing + (ringCount - 1) * perRing * 2) * 3);

        const auto pushVertex = [&mesh, invRadius](float x, float y) {
            mesh.positions.push_back(x);
            mesh.positions.push_back(y);
            mesh.positions.push_back(0.0f);

            mesh.normals.push_back(0.0f);
            mesh.normals.push_back(0.0f);
            mesh.normals.push_back(1.0f);

            mesh.texcoords.push_back(x * invRadius * 0.5f + 0.5f);
            mesh.texcoords.push_back(y * invRadius * 0.5f + 0.5f);
            };

        const auto vertexIndex = [perRing](int ring, int j) -> uint32_t {
            const int wrapped = ((j % perRing) + perRing) % perRing;
            return uint32_t(1 + (ring - 1) * perRing + wrapped);
            };

        // Центр.
        pushVertex(0.0f, 0.0f);

        // Кольца 1..ringCount — тот же шестиугольник, сжатый к центру.
        for (int r = 1; r <= ringCount; ++r) {
            const float t = float(r) / float(ringCount);
            for (int j = 0; j < perRing; ++j) {
                const float angle = 2.0f * kPi * float(j) / float(perRing);
                const float boundary = hexBoundaryRadius(angle, radius) * t;
                pushVertex(std::cos(angle) * boundary, std::sin(angle) * boundary);
            }
        }

        // Веер вокруг центра (CCW при взгляде с +Z).
        for (int j = 0; j < perRing; ++j) {
            mesh.indices.push_back(0);
            mesh.indices.push_back(vertexIndex(1, j));
            mesh.indices.push_back(vertexIndex(1, j + 1));
        }

        // Пояса между соседними кольцами — по два треугольника на сектор.
        for (int r = 1; r < ringCount; ++r) {
            for (int j = 0; j < perRing; ++j) {
                const uint32_t a = vertexIndex(r, j);
                const uint32_t b = vertexIndex(r + 1, j);
                const uint32_t c = vertexIndex(r + 1, j + 1);
                const uint32_t d = vertexIndex(r, j + 1);

                mesh.indices.push_back(a);
                mesh.indices.push_back(b);
                mesh.indices.push_back(c);

                mesh.indices.push_back(a);
                mesh.indices.push_back(c);
                mesh.indices.push_back(d);
            }
        }

        return mesh;
    }

} // namespace

ContributorAsset buildContributorAsset() {
    ContributorAsset asset;

    // =====================================================================
    // ПЕСОЧНИЦА (contributor-режим): вместо дерева — один шестиугольник,
    // на котором обкатываем свет, нормал-маппинг и тайлинг текстур.
    // =====================================================================
    if (defaultAppViewConfig().isContributorMode()) {
        asset.source = ContributorAssetSource::GeneratedMesh;

        const HexSandboxParams hexParams;
        asset.generatedWoodMesh = buildHexSandboxMesh(hexParams);

        qDebug() << "Sandbox hexagon:"
            << asset.generatedWoodMesh.vertexCount() << "vertices,"
            << asset.generatedWoodMesh.triangleCount() << "triangles,"
            << asset.generatedWoodMesh.texcoords.size() / 2 << "uv";

        // ========== НАСТРОЙКИ РЕНДЕРИНГА (для Contributor-режима) ==========
        asset.render.position = QVector3D(0.0f, 0.0f, 0.0f);
        asset.render.rotationDegrees = QVector3D(0.0f, 0.0f, 0.0f);
        asset.render.scale = 0.5f;                                   // радиус 1.0 * 0.5
        asset.render.fallbackColor = QVector3D(0.78f, 0.76f, 0.73f); // серый камень
        asset.woodColor = QVector3D(0.78f, 0.76f, 0.73f);            // этим цветом красится "ствол"
        asset.leavesColor = asset.woodColor;

        // Кроны и частиц в песочнице нет. Кэш деревьев здесь тоже не нужен:
        // он остаётся планетному режиму и экономит время запуска песочницы.
        asset.particles.clear();

        return asset;
    }

    // =====================================================================
    // ПЛАНЕТА: кэш процедурных деревьев 7 видов × N вариантов (без изменений).
    // =====================================================================
    asset.source = ContributorAssetSource::GeneratedMesh;
    appendGeneratedStylizedTreeMeshes(asset);

    // ========== НАСТРОЙКИ РЕНДЕРИНГА (для Contributor-Режима) ==========
    const float treeScale = 0.5f;

    asset.render.position = QVector3D(0.0f, 0.0f, 0.0f);
    asset.render.rotationDegrees = QVector3D(0.0f, 0.0f, 0.0f);
    asset.render.scale = treeScale;
    asset.render.fallbackColor = QVector3D(0.24f, 0.62f, 0.22f);
    asset.woodColor = QVector3D(0.46f, 0.27f, 0.12f);
    asset.leavesColor = QVector3D(0.18f, 0.58f, 0.20f);

    // Для обратной совместимости с contributor-режимом оставляем
    // один Oak-вариант в старых полях.
    if (!asset.speciesMeshes.empty() && !asset.speciesMeshes[0].empty()) {
        asset.generatedWoodMesh = asset.speciesMeshes[0][0];
        asset.particles = asset.speciesParticles[0][0];
    }

    return asset;
}