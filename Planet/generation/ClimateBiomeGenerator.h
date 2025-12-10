#pragma once
#include "model/HexSphereModel.h"
#include "PerlinNoise.h"

class ClimateBiomeGenerator {
public:
    struct ClimateParams {
        float elevationScale = 3.0f;
        float temperatureScale = 2.0f;
        float humidityScale = 2.5f;
        float seaLevel = 0.3f;
        uint32_t seed = 12345;
        float pressureScale = 1.8f;    // Новый параметр
        float oreScale = 4.0f;         // Новый параметр
    };

    void generate(HexSphereModel& model, const ClimateParams& params);

private:
    float calculateElevation(const QVector3D& position, const ClimateParams& params, Perlin3D& elevationNoise);
    float calculateTemperature(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& tempNoise);
    float calculateHumidity(const QVector3D& position, const ClimateParams& params, Perlin3D& humidityNoise);
    float calculatePressure(const QVector3D& position, const ClimateParams& params, Perlin3D& pressureNoise);
    float calculateOreDensity(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& oreNoise);
    uint8_t determineOreType(float oreDensity, float elevation);
    Biome determineBiome(float elevation, float temperature, float humidity, float seaLevel);
};