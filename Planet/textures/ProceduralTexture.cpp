#include "textures/ProceduralTexture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

    // Хеш узла решётки. Отдаёт 32-битное число: из него берём направления
    // градиентов, поэтому нужен весь диапазон битов, а не только [0, 1).
    inline uint32_t hashLattice(int x, int y, uint32_t seed) {
        uint32_t h = uint32_t(x) * 374761393u
            + uint32_t(y) * 668265263u
            + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= (h >> 16);
        return h;
    }

    inline float gradientNoisePeriodic(float x, float y, int period, uint32_t seed) {
        const float fx = std::floor(x);
        const float fy = std::floor(y);
        const int ix = int(fx);
        const int iy = int(fy);
        const float tx = x - fx;
        const float ty = y - fy;

        const auto wrap = [period](int value) {
            return ((value % period) + period) % period;
            };

        const auto gradient = [seed](int gx, int gy, float& outX, float& outY) {
            const uint32_t h = hashLattice(gx, gy, seed);
            const float angle = float(h & 15u) * 0.39269908f;   // 16 направлений
            outX = std::cos(angle);
            outY = std::sin(angle);
            };

        float gx = 0.0f;
        float gy = 0.0f;

        gradient(wrap(ix), wrap(iy), gx, gy);
        const float n00 = gx * tx + gy * ty;
        gradient(wrap(ix + 1), wrap(iy), gx, gy);
        const float n10 = gx * (tx - 1.0f) + gy * ty;
        gradient(wrap(ix), wrap(iy + 1), gx, gy);
        const float n01 = gx * tx + gy * (ty - 1.0f);
        gradient(wrap(ix + 1), wrap(iy + 1), gx, gy);
        const float n11 = gx * (tx - 1.0f) + gy * (ty - 1.0f);

        // Квинтическая интерполяция: убирает разрывы второй производной,
        // из-за которых по границам ячеек были видны "швы" решётки.
        const float u = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
        const float v = ty * ty * ty * (ty * (ty * 6.0f - 15.0f) + 10.0f);
        const float a = n00 + (n10 - n00) * u;
        const float b = n01 + (n11 - n01) * u;
        return (a + (b - a) * v) * 1.4142f;   // примерно к [-1, 1]
    }

    // fbm: градиентный шум + "хребты". Периодичность сохранена, текстура бесшовная.
    float rockHeightAt(float u, float v, const RockTextureParams& params, int basePeriod) {
        float sum = 0.0f;
        float ridge = 0.0f;
        float amp = 0.5f;
        int period = std::max(basePeriod, 1);
        const int octaves = std::max(params.octaves, 1);

        for (int octave = 0; octave < octaves; ++octave) {
            const float offsetX = 13.7f * float(octave);
            const float offsetY = 7.3f * float(octave);

            // 0.5 + 0.5 * ... переводит градиентный шум из [-1, 1] в [0, 1],
            // как раньше отдавал valueNoise (остальной код менять не нужно).
            const float n = 0.5f + 0.5f * gradientNoisePeriodic(
                u * float(period) + offsetX,
                v * float(period) + offsetY,
                period,
                params.seed + uint32_t(octave) * 7919u);

            sum += amp * n;
            ridge += amp * (1.0f - std::fabs(n * 2.0f - 1.0f));

            amp *= params.gain;
            period *= 2;
        }

        const float ridgeMix = std::clamp(params.ridgeMix, 0.0f, 1.0f);
        return sum * (1.0f - ridgeMix) + ridge * ridgeMix;
    }

    inline float sampleHeightWrapped(const std::vector<float>& field, int size, int x, int y) {
        const int wx = ((x % size) + size) % size;
        const int wy = ((y % size) + size) % size;
        return field[size_t(wy) * size + wx];
    }

    inline uchar toByte(float value) {
        return uchar(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    }

} // namespace

RockTextureSet generateRockTextures(const RockTextureParams& params) {
    RockTextureSet set;

    const int size = std::max(params.size, 4);
    const int basePeriod = std::max(int(params.noiseScale + 0.5f), 1);

    set.albedo = QImage(size, size, QImage::Format_RGB888);
    set.normal = QImage(size, size, QImage::Format_RGB888);
    set.roughness = QImage(size, size, QImage::Format_RGB888);
    set.height = QImage(size, size, QImage::Format_RGB888);

    // ---------- 1. Поле высот ----------
    set.heightField.assign(size_t(size) * size, 0.0f);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            set.heightField[size_t(y) * size + x] = rockHeightAt(u, v, params, basePeriod);
        }
    }

    // ---------- 2. Производные и запекание ----------
    const float texel = 1.0f / float(size);
    const float differenceToDerivative = 0.5f / texel;

    for (int y = 0; y < size; ++y) {
        uchar* albedoRow = set.albedo.scanLine(y);
        uchar* normalRow = set.normal.scanLine(y);
        uchar* roughRow = set.roughness.scanLine(y);
        uchar* heightRow = set.height.scanLine(y);

        for (int x = 0; x < size; ++x) {
            const float hC = sampleHeightWrapped(set.heightField, size, x, y);
            const float hL = sampleHeightWrapped(set.heightField, size, x - 1, y);
            const float hR = sampleHeightWrapped(set.heightField, size, x + 1, y);
            const float hD = sampleHeightWrapped(set.heightField, size, x, y - 1);
            const float hU = sampleHeightWrapped(set.heightField, size, x, y + 1);

            const float dhdu = (hR - hL) * differenceToDerivative;
            const float dhdv = (hU - hD) * differenceToDerivative;

            // Нормаль в касательном пространстве — формула ровно как в шейдере.
            const float nx = -dhdu * params.normalStrength;
            const float ny = -dhdv * params.normalStrength;
            const float nz = 1.0f;
            const float invLength = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);

            // Альбедо: трещины темнее, гребни светлее.
            const float shade = params.creviceDarken
                + (params.creviceLighten - params.creviceDarken) * hC;

            // Шероховатость: в трещинах камень грубее.
            const float rough = std::clamp(
                params.roughnessBase + (0.5f - hC) * params.roughnessVariation * 2.0f,
                0.0f, 1.0f);

            albedoRow[x * 3 + 0] = toByte(params.albedoR * shade);
            albedoRow[x * 3 + 1] = toByte(params.albedoG * shade);
            albedoRow[x * 3 + 2] = toByte(params.albedoB * shade);

            normalRow[x * 3 + 0] = toByte(nx * invLength * 0.5f + 0.5f);
            normalRow[x * 3 + 1] = toByte(ny * invLength * 0.5f + 0.5f);
            normalRow[x * 3 + 2] = toByte(nz * invLength * 0.5f + 0.5f);

            roughRow[x * 3 + 0] = toByte(rough);
            roughRow[x * 3 + 1] = toByte(rough);
            roughRow[x * 3 + 2] = toByte(rough);

            heightRow[x * 3 + 0] = toByte(hC);
            heightRow[x * 3 + 1] = toByte(hC);
            heightRow[x * 3 + 2] = toByte(hC);
        }
    }

    return set;
}

std::array<RockTextureSet, kRockMaterialVariantCount> generateRockTextureVariants(const RockTextureParams& params) {
    std::array<RockTextureSet, kRockMaterialVariantCount> result;
    for (int i = 0; i < kRockMaterialVariantCount; ++i) {
        RockTextureParams variant = params;
        variant.seed += static_cast<uint32_t>(i) * 7919u;
        if (i == 1) {
            variant.noiseScale *= 0.75f;
            variant.ridgeMix = 0.55f;
            variant.albedoR *= 0.94f;
            variant.albedoB *= 1.05f;
            variant.normalStrength *= 1.12f;
        }
        else if (i == 2) {
            variant.noiseScale *= 1.35f;
            variant.ridgeMix = 0.22f;
            variant.albedoR *= 1.06f;
            variant.albedoB *= 0.95f;
            variant.normalStrength *= 0.85f;
        }
        result[static_cast<size_t>(i)] = generateRockTextures(variant);
    }
    return result;
}
