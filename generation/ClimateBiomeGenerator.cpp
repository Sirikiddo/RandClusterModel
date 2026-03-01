#include "generation/ClimateBiomeGenerator.h"
#include <cmath>
#include <algorithm>
#include <random>

void ClimateBiomeGenerator::generate(HexSphereModel& model, const ClimateParams& params) {
    Perlin3D elevationNoise(params.seed);
    Perlin3D tempNoise(params.seed + 1000);
    Perlin3D humidityNoise(params.seed + 2000);
    Perlin3D pressureNoise(params.seed + 3000);    // ИЗ ВЕРСИИ 1
    Perlin3D oreNoise(params.seed + 4000);         // ИЗ ВЕРСИИ 1

    auto& cells = model.cells();
    std::srand(params.seed); // для случайных руд

    for (auto& cell : cells) {
        const QVector3D& position = cell.centroid.normalized();

        float elevation = calculateElevation(position, params, elevationNoise);
        float temperature = calculateTemperature(position, elevation, params, tempNoise);
        float humidity = calculateHumidity(position, params, humidityNoise);

        // НОВЫЕ ПАРАМЕТРЫ ИЗ ВЕРСИИ 1
        float pressure = calculatePressure(position, params, pressureNoise);
        float oreDensity = calculateOreDensity(position, elevation, params, oreNoise);

        Biome biome = determineBiome(elevation, temperature, humidity, params.seaLevel);

        cell.height = static_cast<int>((elevation - params.seaLevel) * 10.0f);
        cell.biome = biome;

        // СОХРАНЯЕМ КЛИМАТИЧЕСКИЕ ДАННЫЕ (ИЗ ВЕРСИИ 1)
        cell.temperature = temperature;
        cell.humidity = humidity;
        cell.pressure = pressure;
        cell.oreDensity = oreDensity;
        cell.oreType = determineOreType(oreDensity, elevation);

        // СЛУЧАЙНЫЕ РУДЫ (ИЗ ВЕРСИИ 1)
        if (std::rand() % 100 < 25) {
            cell.oreType = 1 + (std::rand() % 4);
            cell.oreDensity = 0.3f + (std::rand() % 70) / 100.0f;
        }
    }
}

// НОВЫЕ МЕТОДЫ ИЗ ВЕРСИИ 1
float ClimateBiomeGenerator::calculatePressure(const QVector3D& position, const ClimateParams& params, Perlin3D& pressureNoise) {
    float pressure = pressureNoise.noise(
        position.x() * params.pressureScale,
        position.y() * params.pressureScale,
        position.z() * params.pressureScale
    );
    pressure = (pressure + 1.0f) * 0.5f;
    return std::clamp(pressure, 0.0f, 1.0f);
}

float ClimateBiomeGenerator::calculateOreDensity(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& oreNoise) {
    float ore = oreNoise.noise(
        position.x() * params.oreScale,
        position.y() * params.oreScale,
        position.z() * params.oreScale
    );
    if (elevation > 0.7f) {
        ore *= 1.5f;
    }
    ore = (ore + 1.0f) * 0.5f;
    return std::clamp(ore, 0.0f, 1.0f);
}

uint8_t ClimateBiomeGenerator::determineOreType(float oreDensity, float elevation) {
    if (oreDensity < 0.3f) return 0;
    if (elevation > 0.8f) return 1;
    else if (oreDensity > 0.7f) return 2;
    else return 1;
}