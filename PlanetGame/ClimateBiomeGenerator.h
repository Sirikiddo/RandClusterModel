#pragma once
#include "HexSphereModel.h"
#include "PerlinNoise.h"

class ClimateBiomeGenerator {
public:
    struct ClimateParams {
        float elevationScale = 3.0f;
        float temperatureScale = 2.0f;
        float humidityScale = 2.5f;
        float seaLevel = 0.3f;
        uint32_t seed = 12345;
    };

    void generate(HexSphereModel& model, const ClimateParams& params);

private:
    float calculateElevation(const QVector3D& position, const ClimateParams& params, Perlin3D& elevationNoise);
    float calculateTemperature(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& tempNoise);
    float calculateHumidity(const QVector3D& position, const ClimateParams& params, Perlin3D& humidityNoise);
    Biome determineBiome(float elevation, float temperature, float humidity, float seaLevel);
};