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

    void appendGeneratedStylizedTreeMeshes(ContributorAsset& asset) {
        constexpr int kVariantsPerSpecies = 8;
        constexpr float kGlobalScale = 0.12f;

        auto cache = TreeBuilder::generateCache(kVariantsPerSpecies);

        asset.speciesMeshes.resize(TreeBuilder::kTreeSpeciesCount);
        asset.speciesParticles.resize(TreeBuilder::kTreeSpeciesCount);
        asset.speciesVariants = cache;

        for (int s = 0; s < TreeBuilder::kTreeSpeciesCount; ++s) {
            asset.speciesMeshes[s].resize(kVariantsPerSpecies);
            asset.speciesParticles[s].resize(kVariantsPerSpecies);

            for (int v = 0; v < kVariantsPerSpecies; ++v) {
                const auto& variant = cache[s][v];

                // Генерируем меш
                auto meshData = TreeBuilder::generateMesh(variant.params);

                // Меш ствола уже в simple3d::Mesh — просто перемещаем
                asset.speciesMeshes[s][v] = std::move(meshData.wood);

                // Частицы (центры — в локальных координатах дерева, умноженные на globalScale)
                asset.speciesParticles[s][v] =
                    TreeBuilder::generateParticles(meshData, variant, kGlobalScale);
            }

            qDebug() << "Species" << s << "generated"
                << kVariantsPerSpecies << "variants,"
                << asset.speciesParticles[s][0].size() << "particles/variant (first)";
        }

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