#pragma once

#include <QString>
#include <QVector3D>

#include "model/simple3d_parser.hpp"

enum class ContributorAssetSource {
    ModelFile,
    GeneratedMesh
};

struct ContributorRenderSettings {
    QVector3D position{ 0.0f, 0.0f, 0.0f };
    QVector3D rotationDegrees{ 0.0f, 0.0f, 0.0f };
    QVector3D fallbackColor{ 0.24f, 0.62f, 0.22f };
    float scale = 0.5f;
};

struct ContributorAsset {
    ContributorAssetSource source = ContributorAssetSource::ModelFile;

    // Used when source == ModelFile. OBJ and binary STL are supported by ModelHandler.
    QString modelPath = "resources/tree.obj";

    // Used when source == GeneratedMesh. Fill positions, normals/texcoords if available, and indices.
    simple3d::Mesh generatedMesh;
    simple3d::Mesh generatedWoodMesh;
    simple3d::Mesh generatedLeavesMesh;

    ContributorRenderSettings render;
    QVector3D woodColor{ 0.46f, 0.27f, 0.12f };
    QVector3D leavesColor{ 0.18f, 0.58f, 0.20f };
};

// Contributor sandbox entry point. Edit the .cpp next to this header.
ContributorAsset buildContributorAsset();
