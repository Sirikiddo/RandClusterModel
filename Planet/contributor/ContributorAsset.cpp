#include "contributor/ContributorAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <QDebug>
#include <random>
#include <vector>

#include "contributor/ContributorParticles.h"
#include "contributor/TreeBuilder.h"

namespace {

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

        for (int s = 0; s < TreeBuilder::kTreeSpeciesCount; ++s) {
            asset.speciesMeshes[s].resize(kVariantsPerSpecies);
            asset.speciesParticles[s].resize(kVariantsPerSpecies);

            for (int v = 0; v < kVariantsPerSpecies; ++v) {
                // Генерируем меш
                auto meshData = TreeBuilder::generateMesh(cache[s][v].params);

                // Переопределяем видовой коэффициент: теперь это полный множитель
                // размера варианта — видовой характер × нормировка высоты меша.
                cache[s][v].realisticScale =
                    treeScaleForVariant(cache[s][v], localTreeHeight(meshData));

                // Меш ствола уже в simple3d::Mesh — просто перемещаем
                asset.speciesMeshes[s][v] = std::move(meshData.wood);

                // Частицы (центры — в локальных координатах дерева)
                asset.speciesParticles[s][v] =
                    TreeBuilder::generateParticles(meshData, cache[s][v], kGlobalScale);
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

} // namespace

ContributorAsset buildContributorAsset() {
    ContributorAsset asset;

    asset.source = ContributorAssetSource::GeneratedMesh;
    appendGeneratedStylizedTreeMeshes(asset);

    // ========== НАСТРОЙКИ РЕНДЕРИНГА (для Contributor-режима) ==========
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