#pragma once
#include "model/HexSphereModel.h"
#include "generation/PerlinNoise.h"

class ClimateBiomeGenerator {
public:
    struct ClimateParams {
        unsigned seed = 12345;
        float elevationScale = 1.0f;
        float temperatureScale = 1.0f;
        float humidityScale = 1.0f;
        float pressureScale = 1.0f;      // хг бепяхх 1
        float oreScale = 1.0f;            // хг бепяхх 1
        float seaLevel = 0.2f;
    };

    void generate(HexSphereModel& model, const ClimateParams& params);

private:
    float calculateElevation(const QVector3D& position, const ClimateParams& params, Perlin3D& noise);
    float calculateTemperature(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& noise);
    float calculateHumidity(const QVector3D& position, const ClimateParams& params, Perlin3D& noise);
    float calculatePressure(const QVector3D& position, const ClimateParams& params, Perlin3D& noise);      // хг бепяхх 1
    float calculateOreDensity(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& noise); // хг бепяхх 1
    uint8_t determineOreType(float oreDensity, float elevation); // хг бепяхх 1
    Biome determineBiome(float elevation, float temperature, float humidity, float seaLevel);
};