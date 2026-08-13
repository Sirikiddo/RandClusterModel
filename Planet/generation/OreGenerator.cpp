#include "generation/OreGenerator.h"

#include <algorithm>

#include "generation/PerlinNoise.h"
#include "model/HexSphereModel.h"

namespace {
float fbm(Perlin3D& noise, const QVector3D& p, float scale) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float amplitudeSum = 0.0f;
    float frequency = 1.0f;
    for (int octave = 0; octave < 4; ++octave) {
        value += amplitude * noise.noise(
            p.x() * scale * frequency,
            p.y() * scale * frequency,
            p.z() * scale * frequency);
        amplitudeSum += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return std::clamp((value / amplitudeSum + 1.0f) * 0.5f, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

OreType selectOreType(float typeField) {
    const float selector = std::clamp((typeField - 0.30f) / 0.40f, 0.0f, 1.0f);
    if (selector < 0.55f) return OreType::Iron;
    if (selector < 0.80f) return OreType::Copper;
    if (selector < 0.95f) return OreType::Gold;
    return OreType::Diamond;
}
}

void OreGenerator::generate(HexSphereModel& model, uint32_t terrainSeed,
    const OreGenerationParams& params) {
    Perlin3D depositNoise(terrainSeed + 4000u);
    Perlin3D typeNoise(terrainSeed + 5000u);

    for (auto& cell : model.cells()) {
        if (cell.biome != Biome::Rock) {
            cell.oreDensity = 0.0f;
            cell.oreType = OreType::None;
            continue;
        }

        const QVector3D p = cell.centroid.normalized();
        const float geology = fbm(depositNoise, p, params.scale);
        const float density = smoothstep(params.depositThreshold, params.richThreshold, geology);
        if (density <= 0.0f) {
            cell.oreDensity = 0.0f;
            cell.oreType = OreType::None;
            continue;
        }

        cell.oreDensity = density;
        cell.oreType = selectOreType(fbm(typeNoise, p, 1.3f));
    }
}

void OreGenerator::clear(HexSphereModel& model) {
    for (auto& cell : model.cells()) {
        cell.oreDensity = 0.0f;
        cell.oreType = OreType::None;
    }
}
