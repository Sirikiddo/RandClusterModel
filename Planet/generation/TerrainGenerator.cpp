#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"
#include "generation/ClimateBiomeGenerator.h"
#include "generation/PerlinNoise.h"
#include "dag/DataAdapters.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <QString>

int normalizeTerrainGeneratorIndex(int idx) {
    switch (idx) {
    case 0:
    case 1:
    case 2:
    case 3:
        return idx;
    default:
        return 3;
    }
}

std::unique_ptr<ITerrainGenerator> createTerrainGeneratorByIndex(int idx) {
    switch (normalizeTerrainGeneratorIndex(idx)) {
    case 0:
        return std::make_unique<NoOpTerrainGenerator>();
    case 1:
        return std::make_unique<SineTerrainGenerator>();
    case 2:
        return std::make_unique<PerlinTerrainGenerator>();
    case 3:
    default:
        return std::make_unique<ClimateBiomeTerrainGenerator>();
    }
}

namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

bool testFlag(const CoastalBandData& data, int cellId, uint8_t bit) {
    return cellId >= 0 &&
        cellId < static_cast<int>(data.flags.size()) &&
        (data.flags[static_cast<size_t>(cellId)] & bit) != 0;
}

} // namespace

bool CoastalBandData::isSea(int cellId) const { return testFlag(*this, cellId, FlagSea); }
bool CoastalBandData::isShallowSea(int cellId) const { return testFlag(*this, cellId, FlagShallowSea); }
bool CoastalBandData::isBeach(int cellId) const { return testFlag(*this, cellId, FlagBeach); }
bool CoastalBandData::isCoastStepUp(int cellId) const { return testFlag(*this, cellId, FlagCoastStepUp); }
bool CoastalBandData::isFlatCoast(int cellId) const { return testFlag(*this, cellId, FlagFlatCoast); }

int CoastalBandData::distance(int cellId) const {
    if (cellId < 0 || cellId >= static_cast<int>(distanceToShore.size())) {
        return 0;
    }
    return distanceToShore[static_cast<size_t>(cellId)];
}

void normalizeTerrainToWaterModel(HexSphereModel& model) {
    for (auto& cell : model.cells()) {
        if (cell.biome == Biome::Sea) {
            cell.height = std::min(cell.height, -1);
        }
        else {
            cell.height = std::max(cell.height, 0);
        }
    }
}

void generateCanonicalTerrain(
    ITerrainGenerator& generator,
    HexSphereModel& model,
    const TerrainParams& params) {
    generator.generate(model, params);
    normalizeTerrainToWaterModel(model);
}

WaterParams waterParamsForPreset(WaterPreset preset) {
    WaterParams result;
    result.preset = preset;

    switch (preset) {
    case WaterPreset::Lagoon:
        result.shallowColor = QVector3D(0.09f, 0.24f, 0.38f);
        result.deepColor = QVector3D(0.02f, 0.06f, 0.14f);
        result.wetSandColor = QVector3D(0.78f, 0.71f, 0.52f);
        result.drySandColor = QVector3D(0.88f, 0.82f, 0.64f);
        result.waveStrength = 0.72f;
        result.octaveDetail = 0.90f;
        result.specularIntensity = 0.52f;
        result.glintIntensity = 0.88f;
        result.glintThreshold = 0.91f;
        result.glintSharpness = 0.94f;
        result.foamIntensity = 0.12f;
        result.roughness = 0.22f;
        result.reflectionStrength = 0.58f;
        result.opacity = 0.97f;
        result.shellWaveAmplitude = 0.60f;
        result.shellWaveFrequency = 14.0f;
        result.shellWaveSpeed = 0.25f;
        result.depthOpticalDensity = 32.0f;
        result.depthAlphaDensity = 800.0f;
        break;
    case WaterPreset::Storm:
        result.shallowColor = QVector3D(0.06f, 0.15f, 0.27f);
        result.deepColor = QVector3D(0.01f, 0.03f, 0.09f);
        result.wetSandColor = QVector3D(0.70f, 0.63f, 0.45f);
        result.drySandColor = QVector3D(0.80f, 0.74f, 0.56f);
        result.waveStrength = 1.00f;
        result.octaveDetail = 1.00f;
        result.specularIntensity = 0.80f;
        result.glintIntensity = 1.50f;
        result.glintThreshold = 0.86f;
        result.glintSharpness = 0.96f;
        result.foamIntensity = 0.24f;
        result.roughness = 0.09f;
        result.reflectionStrength = 0.82f;
        result.opacity = 0.95f;
        result.shellWaveAmplitude = 1.00f;
        result.shellWaveFrequency = 28.0f;
        result.shellWaveSpeed = 0.75f;
        result.depthOpticalDensity = 48.0f;
        result.depthAlphaDensity = 1000.0f;
        break;
    case WaterPreset::Temperate:
    default:
        result.shallowColor = QVector3D(0.07f, 0.18f, 0.33f);
        result.deepColor = QVector3D(0.01f, 0.035f, 0.10f);
        result.wetSandColor = QVector3D(0.73f, 0.65f, 0.44f);
        result.drySandColor = QVector3D(0.83f, 0.76f, 0.56f);
        result.waveStrength = 0.85f;
        result.octaveDetail = 1.00f;
        result.specularIntensity = 0.62f;
        result.glintIntensity = 1.05f;
        result.glintThreshold = 0.89f;
        result.glintSharpness = 0.92f;
        result.foamIntensity = 0.16f;
        result.roughness = 0.19f;
        result.reflectionStrength = 0.68f;
        result.opacity = 0.96f;
        result.shellWaveAmplitude = 0.84f;
        result.shellWaveFrequency = 21.0f;
        result.shellWaveSpeed = 0.50f;
        result.depthOpticalDensity = 40.0f;
        result.depthAlphaDensity = 1000.0f;
        break;
    }
    return result;
}

WaterParams resolvedWaterParams(const WaterParams& params, const HexSphereModel*) {
    WaterParams result = params;
    result.waveStrength = clamp01(params.waveStrength);
    result.octaveDetail = clamp01(params.octaveDetail);
    result.fresnelStrength = std::clamp(params.fresnelStrength, 0.0f, 2.5f);
    result.specularIntensity = std::clamp(params.specularIntensity, 0.0f, 3.0f);
    result.glintIntensity = std::clamp(params.glintIntensity, 0.0f, 4.0f);
    result.glintThreshold = std::clamp(params.glintThreshold, 0.0f, 0.995f);
    result.glintSharpness = clamp01(params.glintSharpness);
    result.foamIntensity = std::clamp(params.foamIntensity, 0.0f, 3.0f);
    result.roughness = std::clamp(params.roughness, 0.02f, 1.0f);
    result.reflectionStrength = clamp01(params.reflectionStrength);
    result.opacity = std::clamp(params.opacity, 0.70f, 1.0f);
    result.depthOpticalDensity = std::clamp(params.depthOpticalDensity, 0.1f, 1000.0f);
    result.depthAlphaDensity = std::clamp(params.depthAlphaDensity, 0.1f, 1000.0f);
    result.shellWaveAmplitude = std::clamp(params.shellWaveAmplitude, 0.05f, 1.00f);
    result.shellWaveFrequency = std::clamp(params.shellWaveFrequency, 1.0f, 36.0f);
    result.shellWaveSpeed = std::clamp(params.shellWaveSpeed, 0.0f, 1000.0f);
    result.beachWidth = std::max(params.beachWidth, 1);
    return result;
}

CoastalBandData buildCoastalBandData(const HexSphereModel& model, const WaterParams& params) {
    CoastalBandData data;
    const WaterParams water = resolvedWaterParams(params, &model);
    const auto& cells = model.cells();
    const int cellCount = static_cast<int>(cells.size());
    data.waterSurfaceLevel = model.waterSurfaceLevel();
    data.beachWidth = water.beachWidth;
    data.distanceToShore.assign(static_cast<size_t>(cellCount), 0);
    data.shoreEdgeMask.assign(static_cast<size_t>(cellCount), 0u);
    data.shoreVertexMask.assign(static_cast<size_t>(cellCount), 0u);
    data.flags.assign(static_cast<size_t>(cellCount), 0u);
    data.debug.waterSurfaceLevel = data.waterSurfaceLevel;

    std::deque<int> queue;
    std::vector<int> seaDistance(static_cast<size_t>(cellCount), -1);
    for (int cellId = 0; cellId < cellCount; ++cellId) {
        const Cell& cell = cells[static_cast<size_t>(cellId)];
        const bool sea = cell.biome == Biome::Sea;
        if (sea) data.flags[static_cast<size_t>(cellId)] |= CoastalBandData::FlagSea;

        uint8_t edgeMask = 0u;
        bool touchesSea = false;
        const int degree = static_cast<int>(cell.neighbors.size());
        for (int edge = 0; edge < degree; ++edge) {
            const int neighborId = cell.neighbors[static_cast<size_t>(edge)];
            if (neighborId < 0 || neighborId >= cellCount) continue;
            const bool neighborSea = cells[static_cast<size_t>(neighborId)].biome == Biome::Sea;
            touchesSea |= neighborSea;
            if (neighborSea != sea) edgeMask |= static_cast<uint8_t>(1u << edge);
        }

        uint8_t vertexMask = 0u;
        for (int i = 0; i < degree; ++i) {
            const int previous = (i + degree - 1) % degree;
            if (((edgeMask >> i) & 1u) != 0u || ((edgeMask >> previous) & 1u) != 0u) {
                vertexMask |= static_cast<uint8_t>(1u << i);
            }
        }
        data.shoreEdgeMask[static_cast<size_t>(cellId)] = edgeMask;
        data.shoreVertexMask[static_cast<size_t>(cellId)] = vertexMask;

        if (sea && edgeMask != 0u) {
            seaDistance[static_cast<size_t>(cellId)] = 0;
            queue.push_back(cellId);
        }
        if (!sea && touchesSea && cell.height == 0) {
            data.flags[static_cast<size_t>(cellId)] |= CoastalBandData::FlagCoastStepUp;
            ++data.debug.coastStepUpCount;
        }
        if (!sea && touchesSea && cell.height <= std::max(0, data.beachWidth - 1)) {
            data.flags[static_cast<size_t>(cellId)] |= CoastalBandData::FlagBeach;
            ++data.debug.beachCellCount;
        }
    }

    while (!queue.empty()) {
        const int cellId = queue.front();
        queue.pop_front();
        const int nextDistance = seaDistance[static_cast<size_t>(cellId)] + 1;
        for (int neighborId : cells[static_cast<size_t>(cellId)].neighbors) {
            if (neighborId < 0 || neighborId >= cellCount ||
                cells[static_cast<size_t>(neighborId)].biome != Biome::Sea ||
                seaDistance[static_cast<size_t>(neighborId)] >= 0) {
                continue;
            }
            seaDistance[static_cast<size_t>(neighborId)] = nextDistance;
            queue.push_back(neighborId);
        }
    }

    for (int cellId = 0; cellId < cellCount; ++cellId) {
        const int distance = seaDistance[static_cast<size_t>(cellId)];
        if (distance >= 0) {
            data.distanceToShore[static_cast<size_t>(cellId)] = distance;
            data.maxSeaDistance = std::max(data.maxSeaDistance, distance);
            if (distance < data.beachWidth) {
                data.flags[static_cast<size_t>(cellId)] |= CoastalBandData::FlagShallowSea;
                ++data.debug.shallowSeaCount;
            }
        }
        else {
            data.distanceToShore[static_cast<size_t>(cellId)] = data.beachWidth + 1;
        }
    }
    return data;
}

//void NoOpTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& p) {
//    (void)model; (void)p;
//    // Ничего не делаем
//}

void NoOpTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& p) {
    const int n = model.cellCount();

    // Устанавливаем базовые значения для всех клеток
    for (int cid = 0; cid < n; ++cid) {
        // Устанавливаем нулевую высоту (ровная сфера)
        model.setHeight(cid, 0);

        // Используем Sea biome - он уже имеет полупрозрачный голубой цвет
        // в функции biomeColor() из HexSphereModel.h
        model.setBiome(cid, Biome::Rock);

        // Сбрасываем все дополнительные параметры
        model.setTemperature(cid, 0.5f);
        model.setHumidity(cid, 0.5f);
        model.setPressure(cid, 0.5f);
        model.setOreDensity(cid, 0.0f);
        model.setOreType(cid, 0);
    }

    qDebug() << "NoOpTerrainGenerator: Created transparent sphere with" << n << "cells";
}

void SineTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& p) {
    const int n = model.cellCount();
    float amplitude = 3.0f;
    float frequency = p.scale * 2.0f;

    for (int cid = 0; cid < n; ++cid) {
        const auto& cell = model.cells()[cid];
        QVector3D c = cell.centroid;
        float lat = std::asin(c.y());
        float h = std::sin(lat * frequency) * 0.7f + std::sin(lat * frequency * 2.3f) * 0.3f;
        h = h * amplitude;
        converters::HeightSample sample{ h };
        int height = converters::HeightmapAdapter::toDiscreteHeight(sample, amplitude, static_cast<float>(p.seaLevel));
        model.setHeight(cid, height);

        const QString material = converters::MaterialAdapter::pickMaterial(static_cast<float>(height), static_cast<float>(p.seaLevel));
        if (material == "water") {
            model.setBiome(cid, Biome::Sea);
        }
        else if (material == "sand" || material == "grass") {
            model.setBiome(cid, Biome::Grass);
        }
        else {
            model.setBiome(cid, Biome::Rock);
        }
    }
}

void PerlinTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& params) {
    const int n = model.cellCount();
    Perlin3D noise(params.seed);

    for (int cid = 0; cid < n; ++cid) {
        const auto& cell = model.cells()[cid];
        QVector3D point = cell.centroid.normalized();

        float h = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxAmplitude = 0.0f;

        for (int i = 0; i < 4; i++) {
            h += amplitude * noise.noise(
                point.x() * frequency,
                point.y() * frequency,
                point.z() * frequency
            );
            maxAmplitude += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }

        h /= maxAmplitude;
        float heightValue = h * 3.0f;
        converters::HeightSample sample{ heightValue };
        int discreteHeight = converters::HeightmapAdapter::toDiscreteHeight(sample, 1.0f, static_cast<float>(params.seaLevel));
        model.setHeight(cid, discreteHeight);

        const QString material = converters::MaterialAdapter::pickMaterial(static_cast<float>(discreteHeight), static_cast<float>(params.seaLevel));
        if (material == "water") {
            model.setBiome(cid, Biome::Sea);
        }
        else if (material == "sand" || material == "grass") {
            model.setBiome(cid, Biome::Grass);
        }
        else {
            model.setBiome(cid, Biome::Rock);
        }
    }
}

void ClimateBiomeTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& p) {
    ClimateBiomeGenerator climateGenerator;
    ClimateBiomeGenerator::ClimateParams climateParams;

    climateParams.seed = p.seed;
    climateParams.elevationScale = p.scale;
    climateParams.temperatureScale = p.scale * 0.8f;
    climateParams.humidityScale = p.scale * 1.2f;
    constexpr float kClimateElevationPerTerrainLevel = 0.1f;
    climateParams.seaLevel = static_cast<float>(p.seaLevel) * kClimateElevationPerTerrainLevel;

    climateGenerator.generate(model, climateParams);
}
