#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <QVector3D>

class HexSphereModel;

struct TerrainParams {
    uint32_t seed = 0u;
    int      seaLevel = 0;
    float    scale = 3.0f;
};

inline constexpr int kDefaultTerrainGeneratorIndex = 3;
inline constexpr int kDefaultTerrainSubdivisionLevel = 2;

inline constexpr TerrainParams defaultTerrainParams() {
    return TerrainParams{ 0u, 0, 3.0f };
}

enum class WaterPreset : int {
    Temperate = 0,
    Lagoon = 1,
    Storm = 2,
};

struct WaterParams {
    WaterPreset preset = WaterPreset::Temperate;
    float waveStrength = 0.85f;
    int beachWidth = 1;
    float shellWaveAmplitude = 0.84f;
    float shellWaveFrequency = 21.0f;
    float shellWaveSpeed = 0.50f;
    float octaveDetail = 1.00f;
    float fresnelStrength = 0.92f;
    float specularIntensity = 0.55f;
    float glintIntensity = 0.75f;
    float glintThreshold = 0.89f;
    float glintSharpness = 0.92f;
    float foamIntensity = 0.16f;
    float roughness = 0.12f;
    float reflectionStrength = 0.78f;
    float opacity = 1.0f;
    float depthOpticalDensity = 40.0f;
    float depthAlphaDensity = 1000.0f;
    QVector3D shallowColor = QVector3D(0.07f, 0.18f, 0.33f);
    QVector3D deepColor = QVector3D(0.01f, 0.035f, 0.10f);
    QVector3D wetSandColor = QVector3D(0.73f, 0.65f, 0.44f);
    QVector3D drySandColor = QVector3D(0.83f, 0.76f, 0.56f);
};

enum class WaterUpdateKind : uint8_t {
    None,
    OpticsOnly,
    WaveSpec,
    CoastGeometry,
};

struct CoastalBandDebugInfo {
    float waterSurfaceLevel = -0.5f;
    int coastStepUpCount = 0;
    int flatCoastCount = 0;
    int beachCellCount = 0;
    int shallowSeaCount = 0;
};

struct CoastalBandData {
    float waterSurfaceLevel = -0.5f;
    int beachWidth = 1;
    int maxSeaDistance = 0;
    std::vector<int> distanceToShore;
    std::vector<uint8_t> shoreEdgeMask;
    std::vector<uint8_t> shoreVertexMask;
    std::vector<uint8_t> flags;
    CoastalBandDebugInfo debug;

    enum : uint8_t {
        FlagSea = 1 << 0,
        FlagShallowSea = 1 << 1,
        FlagBeach = 1 << 2,
        FlagCoastStepUp = 1 << 3,
        FlagFlatCoast = 1 << 4,
    };

    bool empty() const noexcept { return distanceToShore.empty(); }
    bool isSea(int cellId) const;
    bool isShallowSea(int cellId) const;
    bool isBeach(int cellId) const;
    bool isCoastStepUp(int cellId) const;
    bool isFlatCoast(int cellId) const;
    int distance(int cellId) const;
};

class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;
    virtual void generate(HexSphereModel& model, const TerrainParams& p) = 0;
};

// Ничего не делает: всё «по умолчанию»
class NoOpTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

// Синусоидальная генерация
class SineTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

// Шум Перлина
class PerlinTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

// Климатическая генерация биомов
class ClimateBiomeTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

int normalizeTerrainGeneratorIndex(int idx);
std::unique_ptr<ITerrainGenerator> createTerrainGeneratorByIndex(int idx);
void normalizeTerrainToWaterModel(HexSphereModel& model);
void generateCanonicalTerrain(ITerrainGenerator& generator, HexSphereModel& model, const TerrainParams& params);
WaterParams waterParamsForPreset(WaterPreset preset);
WaterParams resolvedWaterParams(const WaterParams& params, const HexSphereModel* model = nullptr);
CoastalBandData buildCoastalBandData(const HexSphereModel& model, const WaterParams& params);
