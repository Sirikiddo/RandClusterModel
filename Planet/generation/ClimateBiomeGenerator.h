#pragma once
#include "model/HexSphereModel.h"
#include "PerlinNoise.h"

class ClimateBiomeGenerator {
public:
    // The procedural elevation field used to be exposed in [0, 1], making a
    // user-facing sea level of zero incapable of producing any water. Keep
    // the established 0.40 shoreline as the planet's zero-height datum.
    static constexpr float kElevationDatum = 0.40f;

    struct ClimateParams {
        float elevationScale = 3.0f;
        float temperatureScale = 2.0f;
        float humidityScale = 2.5f;
        float seaLevel = 0.0f;
        uint32_t seed = 12345;
        float pressureScale = 1.8f;
    };

    void generate(HexSphereModel& model, const ClimateParams& params);

private:
    float calculateElevation(const QVector3D& position, const ClimateParams& params, Perlin3D& elevationNoise);
    float calculateTemperature(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& tempNoise);
    float calculateHumidity(const QVector3D& position, const ClimateParams& params, Perlin3D& humidityNoise);
    float calculatePressure(const QVector3D& position, const ClimateParams& params, Perlin3D& pressureNoise);
    Biome determineBiome(float elevation, float temperature, float humidity, float seaLevel);
};
