#pragma once

#include <QVector3D>
#include <cstdint>
#include <random>
#include <vector>

#include "model/HexSphereModel.h"           // для Biome
#include "model/simple3d_parser.hpp"        // для simple3d::Mesh
#include "contributor/ContributorParticles.h"

class TreeBuilder {
public:
    enum class TreeSpecies : uint8_t {
        Oak = 0,
        Apple = 1,
        Birch = 2,
        Acacia = 3,
        Kigelia = 4,
        Ceiba = 5,
        Tualang = 6
    };

    static constexpr int kTreeSpeciesCount = 7;

    struct TreeParams {
        int trunkSegments = 14;
        int radialSegments = 10;
        float trunkHeight = 6.0f;
        float trunkRadiusBase = 0.35f;
        float trunkRadiusTop = 0.10f;
        int branchCount = 7;
        int branchSegments = 5;
        float branchLengthMin = 1.0f;
        float branchLengthMax = 2.1f;
        float branchRadiusFactor = 0.38f;
        float branchUpBias = 0.45f;
        float trunkBend = 0.7f;
        float trunkNoise = 0.25f;
        int leafBlobSubdivLat = 6;
        int leafBlobSubdivLon = 8;
        float leafBlobRadiusMin = 0.45f;
        float leafBlobRadiusMax = 0.90f;
        int leafBlobsPerBranch = 2;
        uint32_t seed = 1337;
    };

    struct TreeMeshData {
        simple3d::Mesh wood;                  // ствол + ветки (positions/normals/texcoords/indices)
        std::vector<QVector3D> branchTips;    // концы веток (локальные координаты дерева)
    };

    struct TreeVariant {
        TreeSpecies species = TreeSpecies::Oak;
        TreeParams params;
        QVector3D foliageColor{ 0.20f, 0.60f, 0.20f };
        QVector3D trunkColor{ 0.40f, 0.25f, 0.15f };
        float realisticScale = 1.0f;          // коэффициент (Oak=1.0, Apple=0.3, Tualang=1.6)
        uint32_t seed = 0;
    };

    // Выбор вида по биому и seed
    static TreeSpecies selectSpecies(Biome biome, uint32_t seed);

    // Базовые параметры для вида
    static TreeParams baseParamsForSpecies(TreeSpecies species);

    // Базовые цвета для вида
    static void colorsForSpecies(TreeSpecies species,
        QVector3D& foliageOut,
        QVector3D& trunkOut);

    // Реалистичный размер (коэффициент)
    static float realisticScaleForSpecies(TreeSpecies species);

    // Сгенерировать вариант с вариативностью (±%)
    static TreeVariant generateVariant(TreeSpecies species, uint32_t seed);

    // Сгенерировать меш дерева по параметрам
    static TreeMeshData generateMesh(const TreeParams& params);

    // Сгенерировать частицы кроны.
    // branchTips уже должны быть в локальном масштабе дерева (умножены на globalScale).
    static std::vector<ContributorParticle> generateParticles(
        const TreeMeshData& mesh,
        const TreeVariant& variant,
        float globalScale);

    // Сгенерировать кэш: N вариантов для каждого вида (7 ? N)
    static std::vector<std::vector<TreeVariant>> generateCache(int variantsPerSpecies);
};