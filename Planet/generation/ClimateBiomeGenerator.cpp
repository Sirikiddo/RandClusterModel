#include "generation/ClimateBiomeGenerator.h"
#include <cmath>
#include <algorithm>

void ClimateBiomeGenerator::generate(HexSphereModel& model, const ClimateParams& params) {
    Perlin3D elevationNoise(params.seed);
    Perlin3D tempNoise(params.seed + 1000);
    Perlin3D humidityNoise(params.seed + 2000);

    // Ïîëó÷àåì íåêîíñòàíòíóþ ññûëêó íà ÿ÷åéêè
    auto& cells = model.cells(); // Óáåäèòåñü, ÷òî â HexSphereModel åñòü íåêîíñòàíòíûé ìåòîä cells()

    for (auto& cell : cells) {
        const QVector3D& position = cell.centroid.normalized();

        // 1. Âûñîòà (îñíîâíîé ðåëüåô)
        float elevation = calculateElevation(position, params, elevationNoise);

        // 2. Òåìïåðàòóðà (çàâèñèò îò øèðîòû è âûñîòû)
        float temperature = calculateTemperature(position, elevation, params, tempNoise);

        // 3. Âëàæíîñòü (îòäåëüíûé øóì)
        float humidity = calculateHumidity(position, params, humidityNoise);

        // 4. Îïðåäåëÿåì áèîì ïî òàáëèöå
        Biome biome = determineBiome(elevation, temperature, humidity, params.seaLevel);

        // Óñòàíàâëèâàåì çíà÷åíèÿ - òåïåðü ýòî ðàáîòàåò, ò.ê. cell íå êîíñòàíòíàÿ
        cell.height = static_cast<int>((elevation - params.seaLevel) * 10.0f);
        cell.biome = biome;
    }
}

float ClimateBiomeGenerator::calculateElevation(const QVector3D& position, const ClimateParams& params, Perlin3D& elevationNoise) {
    // Ìíîãîîêòàâíûé øóì äëÿ ñëîæíîãî ðåëüåôà
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
    return (elevation + 1.0f) * 0.5f; // Íîðìàëèçóåì ê [0, 1]
}

float ClimateBiomeGenerator::calculateTemperature(const QVector3D& position, float elevation, const ClimateParams& params, Perlin3D& tempNoise) {
    // Øèðîòà (y-êîîðäèíàòà) - îñíîâíîé ôàêòîð òåìïåðàòóðû
    float latitude = std::abs(position.y()); // 0 = ýêâàòîð, 1 = ïîëþñ

    // Áàçîâàÿ òåìïåðàòóðà: æàðêî íà ýêâàòîðå, õîëîäíî íà ïîëþñàõ
    float baseTemp = 1.0f - latitude;

    // Âûñîòà âëèÿåò íà òåìïåðàòóðó (÷åì âûøå - òåì õîëîäíåå)
    float heightEffect = (elevation - params.seaLevel) * 0.6f;
    baseTemp -= std::max(0.0f, heightEffect);

    // Äîáàâëÿåì øóì äëÿ ðàçíîîáðàçèÿ
    float tempNoiseValue = tempNoise.noise(
        position.x() * params.temperatureScale,
        position.y() * params.temperatureScale,
        position.z() * params.temperatureScale
    ) * 0.2f;

    float temperature = baseTemp + tempNoiseValue;
    return std::clamp(temperature, 0.0f, 1.0f);
}

float ClimateBiomeGenerator::calculateHumidity(const QVector3D& position, const ClimateParams& params, Perlin3D& humidityNoise) {
    // Âëàæíîñòü - ÷èñòûé øóì ñ íåáîëüøèìè êîððåëÿöèÿìè
    float humidity = humidityNoise.noise(
        position.x() * params.humidityScale + 50.0f,
        position.y() * params.humidityScale,
        position.z() * params.humidityScale + 25.0f
    );

    // Íîðìàëèçóåì ê [0, 1]
    humidity = (humidity + 1.0f) * 0.5f;
    return std::clamp(humidity, 0.0f, 1.0f);
}

Biome ClimateBiomeGenerator::determineBiome(float elevation, float temperature, float humidity, float seaLevel) {
    // Òàáëèöà áèîìîâ ïî êëèìàòè÷åñêèì ïàðàìåòðàì:

    // 1. Îêåàí/Ìîðå - âñ¸ ÷òî íèæå óðîâíÿ ìîðÿ
    if (elevation < seaLevel) {
        return Biome::Sea;
    }

    // 2. Ãîðû/Êàìíè - î÷åíü âûñîêèå îáëàñòè
    if (elevation > 0.85f) {
        return Biome::Rock;
    }

    // 3. Îñòàëüíûå áèîìû ïî òåìïåðàòóðå è âëàæíîñòè
    if (temperature < 0.25f) {
        // Î÷åíü õîëîäíî - ñíåã
        return Biome::Snow;
    }
    else if (temperature < 0.45f) {
        // Õîëîäíî - òóíäðà èëè òðàâà
        return (humidity < 0.5f) ? Biome::Tundra : Biome::Grass;
    }
    else if (temperature < 0.7f) {
        // Óìåðåííî - ïóñòûíÿ èëè ñàâàííà/òðàâà
        if (humidity < 0.3f) return Biome::Desert;
        else if (humidity < 0.6f) return Biome::Savanna;
        else return Biome::Grass;
    }
    else {
        // Æàðêî - äæóíãëè èëè ñàâàííà
        return (humidity > 0.6f) ? Biome::Jungle : Biome::Savanna;
    }
}