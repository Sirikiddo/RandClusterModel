#include "ClimateBiomeGenerator.h"
#include <cmath>
#include <algorithm>

void ClimateBiomeGenerator::generate(HexSphereModel& model, const ClimateParams& params) {
    Perlin3D elevationNoise(params.seed);
    Perlin3D tempNoise(params.seed + 1000);
    Perlin3D humidityNoise(params.seed + 2000);
    Perlin3D pressureNoise(params.seed + 3000);
    Perlin3D oreNoise(params.seed + 4000);

    auto& cells = model.cells();

    std::srand(params.seed);

    for (auto& cell : cells) {

        const QVector3D& position = cell.centroid.normalized();

        // 1. Высота (основной рельеф)
        float elevation = calculateElevation(position, params, elevationNoise);

        // 2. Температура (зависит от широты и высоты)
        float temperature = calculateTemperature(position, elevation, params, tempNoise);

        // 3. Влажность (отдельный шум)
        float humidity = calculateHumidity(position, params, humidityNoise);

        // 4. Давление (новый параметр)
        float pressure = calculatePressure(position, params, pressureNoise);

        // 5. Плотность руды (новый параметр)
        float oreDensity = calculateOreDensity(position, elevation, params, oreNoise);

        // 6. Определяем биом по таблице
        Biome biome = determineBiome(elevation, temperature, humidity, params.seaLevel);

        // Устанавливаем значения
        cell.height = static_cast<int>((elevation - params.seaLevel) * 10.0f);
        cell.biome = biome;

        // Сохраняем климатические данные
        cell.temperature = temperature;
        cell.humidity = humidity;
        cell.pressure = pressure;
        cell.oreDensity = oreDensity;
        cell.oreType = determineOreType(oreDensity, elevation);

        if (std::rand() % 100 < 25) {
            cell.oreType = 1 + (std::rand() % 4); // тип 1-4
            cell.oreDensity = 0.3f + (std::rand() % 70) / 100.0f; // плотность 0.3-1.0
        }
    }
}

float ClimateBiomeGenerator::calculatePressure(const QVector3D& position, const ClimateParams& params, Perlin3D& pressureNoise) {
    // Давление зависит от высоты и шума
    float pressure = pressureNoise.noise(
        position.x() * params.pressureScale,
        position.y() * params.pressureScale,
        position.z() * params.pressureScale
    );

    // Нормализуем к [0, 1]
    pressure = (pressure + 1.0f) * 0.5f;
    return std::clamp(pressure, 0.0f, 1.0f);
}

float ClimateBiomeGenerator::calculateOreDensity(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& oreNoise) {
    // Плотность руды зависит от высоты и отдельного шума
    float ore = oreNoise.noise(
        position.x() * params.oreScale,
        position.y() * params.oreScale,
        position.z() * params.oreScale
    );

    // Увеличиваем вероятность руды в горах
    if (elevation > 0.7f) {
        ore *= 1.5f;
    }

    ore = (ore + 1.0f) * 0.5f;
    return std::clamp(ore, 0.0f, 1.0f);
}

uint8_t ClimateBiomeGenerator::determineOreType(float oreDensity, float elevation) {
    if (oreDensity < 0.3f) return 0; // Нет руды

    // Определяем тип руды на основе плотности и высоты
    if (elevation > 0.8f) {
        return 1; // Горная руда (железо)
    }
    else if (oreDensity > 0.7f) {
        return 2; // Богатая руда (медь)
    }
    else {
        return 1; // Обычная руда (железо)
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