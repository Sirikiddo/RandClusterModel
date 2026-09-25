#pragma once

#include <QString>
#include <QVector3D>
#include <vector>

#include "model/simple3d_parser.hpp"
#include "contributor/ContributorParticles.h"
#include "contributor/TreeBuilder.h"

enum class ContributorAssetSource {
    ModelFile,
    GeneratedMesh
};

struct ContributorTreeModel {
    simple3d::Mesh woodMesh;
    std::vector<QVector3D> branchTips;
    QVector3D woodColor{ 0.46f, 0.27f, 0.12f };
    QVector3D leavesColor{ 0.18f, 0.58f, 0.20f };
    QVector3D position{ 0.0f, 0.0f, 0.0f };
};

struct ContributorRenderSettings {
    QVector3D position{ 0.0f, 0.0f, 0.0f };
    QVector3D rotationDegrees{ 0.0f, 0.0f, 0.0f };
    QVector3D fallbackColor{ 0.24f, 0.62f, 0.22f };
    float scale = 0.5f;
};

struct ContributorAsset {
    ContributorAssetSource source = ContributorAssetSource::ModelFile;
    QString modelPath = "resources/default_tree.obj";
    simple3d::Mesh generatedMesh;
    simple3d::Mesh generatedWoodMesh;
    simple3d::Mesh generatedLeavesMesh;
    ContributorRenderSettings render;
    QVector3D woodColor{ 0.46f, 0.27f, 0.12f };
    QVector3D leavesColor{ 0.18f, 0.58f, 0.20f };
    std::vector<ContributorParticle> particles;
    std::vector<QVector3D> branchTips;
    std::vector<ContributorTreeModel> trees;

    // ========== НОВЫЙ КЭШ ПО ВИДАМ (7 × N) ==========
    // Индексация: [species][variant]
    std::vector<std::vector<simple3d::Mesh>> speciesMeshes;
    std::vector<std::vector<std::vector<ContributorParticle>>> speciesParticles;
    std::vector<std::vector<TreeBuilder::TreeVariant>> speciesVariants;
};

// Contributor sandbox entry point. Edit the .cpp next to this header.
ContributorAsset buildContributorAsset();