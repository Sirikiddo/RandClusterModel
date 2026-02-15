#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"
#include "generation/ClimateBiomeGenerator.h"
#include "generation/PerlinNoise.h"
#include "tools/converters/DataAdapters.h"
#include <cmath>
#include <QString>

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
    climateParams.seaLevel = p.seaLevel * 0.1f;

    climateGenerator.generate(model, climateParams);
}