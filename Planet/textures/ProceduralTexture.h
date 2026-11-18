#pragma once

#include <QImage>
#include <array>
#include <cstdint>
#include <vector>

struct RockTextureParams {
    int size = 512;              // разрешение, степень двойки (512 или 1024)
    float noiseScale = 6.0f;     // базовая частота = число ячеек шума на период
    int octaves = 5;
    float ridgeMix = 0.35f;      // доля "хребтов" 1-|2n-1| (грани, сколы)
    float gain = 0.5f;           // затухание амплитуды по октавам
    float normalStrength = 0.16f; // сила рельефа, с которой печём нормали
    float albedoR = 0.46f;       // базовый цвет камня
    float albedoG = 0.45f;
    float albedoB = 0.43f;
    float creviceDarken = 0.55f; // множитель цвета в трещинах
    float creviceLighten = 1.15f;// множитель цвета на гребнях
    float roughnessBase = 0.85f; // средняя шероховатость
    float roughnessVariation = 0.12f;
    uint32_t seed = 1337;
};

struct RockTextureSet {
    QImage albedo;      // RGB888
    QImage normal;      // RGB888, касательное пространство
    QImage roughness;   // RGB888 (серое)
    QImage height;      // RGB888 (серое)
    std::vector<float> heightField;   // сырые высоты size*size (для масок блендинга)
};

RockTextureSet generateRockTextures(const RockTextureParams& params);

inline constexpr int kRockMaterialVariantCount = 3;
std::array<RockTextureSet, kRockMaterialVariantCount> generateRockTextureVariants(const RockTextureParams& params);
