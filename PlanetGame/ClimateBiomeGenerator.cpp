#include "ClimateBiomeGenerator.h"
#include <cmath>
#include <algorithm>

void ClimateBiomeGenerator::generate(HexSphereModel& model, const ClimateParams& params) {
    Perlin3D elevationNoise(params.seed);
    Perlin3D tempNoise(params.seed + 1000);
    Perlin3D humidityNoise(params.seed + 2000);

    // Получаем неконстантную ссылку на ячейки
    auto& cells = model.cells(); // Убедитесь, что в HexSphereModel есть неконстантный метод cells()

    for (auto& cell : cells) {
        const QVector3D& position = cell.centroid.normalized();

        // 1. Высота (основной рельеф)
        float elevation = calculateElevation(position, params, elevationNoise);

        // 2. Температура (зависит от широты и высоты)
        float temperature = calculateTemperature(position, elevation, params, tempNoise);

        // 3. Влажность (отдельный шум)
        float humidity = calculateHumidity(position, params, humidityNoise);

        // 4. Определяем биом по таблице
        Biome biome = determineBiome(elevation, temperature, humidity, params.seaLevel);

        // Устанавливаем значения - теперь это работает, т.к. cell не константная
        cell.height = static_cast<int>((elevation - params.seaLevel) * 10.0f);
        cell.biome = biome;
    }
}

float ClimateBiomeGenerator::calculateElevation(const QVector3D& position, const ClimateParams& params, Perlin3D& elevationNoise) {
    // Многооктавный шум для сложного рельефа
    float elevation = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < 4; i++) {
        elevation += amplitude * elevationNoise.noise(
            position.x() * frequency * params.elevationScale,
            position.y() * frequency * params.elevationScale,
            position.z() * frequency * params.elevationScale
        );
        maxAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    elevation /= maxAmplitude;
    return (elevation + 1.0f) * 0.5f; // Нормализуем к [0, 1]
}

float ClimateBiomeGenerator::calculateTemperature(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& tempNoise) {
    // Широта (y-координата) - основной фактор температуры
    float latitude = std::abs(position.y()); // 0 = экватор, 1 = полюс

    // Базовая температура: жарко на экваторе, холодно на полюсах
    float baseTemp = 1.0f - latitude;

    // Высота влияет на температуру (чем выше - тем холоднее)
    float heightEffect = (elevation - params.seaLevel) * 0.6f;
    baseTemp -= std::max(0.0f, heightEffect);

    // Добавляем шум для разнообразия
    float tempNoiseValue = tempNoise.noise(
        position.x() * params.temperatureScale,
        position.y() * params.temperatureScale,
        position.z() * params.temperatureScale
    ) * 0.2f;

    float temperature = baseTemp + tempNoiseValue;
    return std::clamp(temperature, 0.0f, 1.0f);
}

float ClimateBiomeGenerator::calculateHumidity(const QVector3D& position, const ClimateParams& params, Perlin3D& humidityNoise) {
    // Влажность - чистый шум с небольшими корреляциями
    float humidity = humidityNoise.noise(
        position.x() * params.humidityScale + 50.0f,
        position.y() * params.humidityScale,
        position.z() * params.humidityScale + 25.0f
    );

    // Нормализуем к [0, 1]
    humidity = (humidity + 1.0f) * 0.5f;
    return std::clamp(humidity, 0.0f, 1.0f);
}

Biome ClimateBiomeGenerator::determineBiome(float elevation, float temperature, float humidity, float seaLevel) {
    // Таблица биомов по климатическим параметрам:

    // 1. Океан/Море - всё что ниже уровня моря
    if (elevation < seaLevel) {
        return Biome::Sea;
    }

    // 2. Горы/Камни - очень высокие области
    if (elevation > 0.85f) {
        return Biome::Rock;
    }

    // 3. Остальные биомы по температуре и влажности
    if (temperature < 0.25f) {
        // Очень холодно - снег
        return Biome::Snow;
    }
    else if (temperature < 0.45f) {
        // Холодно - тундра или трава
        return (humidity < 0.5f) ? Biome::Tundra : Biome::Grass;
    }
    else if (temperature < 0.7f) {
        // Умеренно - пустыня или саванна/трава
        if (humidity < 0.3f) return Biome::Desert;
        else if (humidity < 0.6f) return Biome::Savanna;
        else return Biome::Grass;
    }
    else {
        // Жарко - джунгли или саванна
        return (humidity > 0.6f) ? Biome::Jungle : Biome::Savanna;
    }
}