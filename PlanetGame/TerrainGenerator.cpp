#include "TerrainGenerator.h"
#include "HexSphereModel.h"
#include <cmath>
#include "PerlinNoise.h"

void NoOpTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& p) {
    (void)model; (void)p;
    // Ничего не делаем: оставляем высоты/биомы как есть.
}

void SineTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& p) {
    const int n = model.cellCount();

    // Автоматически подбираем амплитуду в зависимости от уровня детализации
    float amplitude = 3.0f; // базовая амплитуда
    float frequency = p.scale * 2.0f; // увеличиваем частоту

    for (int cid = 0; cid < n; ++cid) {
        const auto& cell = model.cells()[cid];
        QVector3D c = cell.centroid;

        // Используем несколько синусов для более естественного рельефа
        float lat = std::asin(c.y()); // правильная широта в радианах [-π/2, π/2]

        // Многослойный синус для естественного рельефа
        float h = std::sin(lat * frequency) * 0.7f
            + std::sin(lat * frequency * 2.3f) * 0.3f;

        // Нормализуем к диапазону [-amplitude, amplitude]
        h = h * amplitude;

        // Плавное распределение биомов на основе высоты
        int height = static_cast<int>(std::round(h));

        model.setHeight(cid, height);

        // Более плавное распределение биомов
        if (h < p.seaLevel - 0.5f) {
            model.setBiome(cid, Biome::Sea);
        }
        else if (h < p.seaLevel + 1.0f) {
            model.setBiome(cid, Biome::Grass);
        }
        else {
            model.setBiome(cid, Biome::Rock);
        }
    }
}

void PerlinTerrainGenerator::generate(HexSphereModel& model, const TerrainParams& params) {
    const int n = model.cellCount();
    Perlin3D noise(params.seed); // Используем seed из параметров

    for (int cid = 0; cid < n; ++cid) {
        const auto& cell = model.cells()[cid];
        QVector3D point = cell.centroid.normalized();

        // Многооктавный шум Перлина
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
            amplitude *= 0.5f;    // Уменьшаем амплитуду
            frequency *= 2.0f;    // Увеличиваем частоту
        }

        // Нормализуем и масштабируем
        h /= maxAmplitude;
        float heightValue = h * 3.0f; // Диапазон примерно [-3, 3]

        int discreteHeight = static_cast<int>(std::round(heightValue));
        model.setHeight(cid, discreteHeight);

        // Распределение биомов - используем params.seaLevel
        if (heightValue < params.seaLevel - 0.5f) {
            model.setBiome(cid, Biome::Sea);
        }
        else if (heightValue < params.seaLevel + 1.0f) {
            model.setBiome(cid, Biome::Grass);
        }
        else {
            model.setBiome(cid, Biome::Rock);
        }
    }
}