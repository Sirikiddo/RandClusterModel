#include "generation/WaterWaveModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr float kPhasePerScanSegment = kPi / 8.0f;
constexpr float kMinimumAxisSeparationRadians = 15.0f * kPi / 180.0f;
constexpr float kMaximumLocalModeWeight = 0.16f;
constexpr float kInverseSqrtTwo = 0.7071067811865475244f;

WaterSpectrumBand makeSpectrumBand(
    int count,
    float lowMinimum,
    float lowMaximum,
    float highMinimum,
    float highMaximum,
    float bandShare,
    float spread,
    float coupling,
    float packetOuter,
    float packetContrast,
    float activityFloor,
    float activityCycle,
    float mixingCycle) {
    WaterSpectrumBand result;
    result.modeCount = count;
    result.lowDetailMinFrequencyMultiplier = lowMinimum;
    result.lowDetailMaxFrequencyMultiplier = lowMaximum;
    result.minFrequencyMultiplier = highMinimum;
    result.maxFrequencyMultiplier = highMaximum;
    result.energy = bandShare;
    result.directionSpreadDegrees = spread;
    result.phaseCouplingRadians = coupling;
    result.packetInnerAngleDegrees = 20.0f;
    result.packetOuterAngleDegrees = packetOuter;
    result.packetContrast = packetContrast;
    result.activityFloor = activityFloor;
    result.activityCycleSeconds = activityCycle;
    result.mixingCycleSeconds = mixingCycle;
    return result;
}

WaterSpectrumConfig makeSpectrumConfig(WaterPreset preset) {
    WaterSpectrumConfig result;
    result.seed = 0xBF8574E2u;
    result.windAxis = QVector3D(0.86f, 0.10f, 0.50f);
    result.groupVelocityRatio = 0.5f;

    switch (preset) {
    case WaterPreset::Lagoon:
        result.isotropicFraction = 0.60f;
        result.couplingDriverFrequencyScale = 0.42f;
        result.crestBreakupStrength = 0.76f;
        result.crestBreakupFrequencyMultiplier = 1.45f;
        result.bands = {{
            makeSpectrumBand(5, 0.82f, 1.18f, 0.82f, 1.18f, 0.42f, 78.0f, 1.05f, 82.0f, 0.90f, 0.24f, 37.0f, 29.0f),
            makeSpectrumBand(4, 0.92f, 1.28f, 1.43f, 1.78f, 0.34f, 86.0f, 0.72f, 76.0f, 0.88f, 0.24f, 29.0f, 23.0f),
            makeSpectrumBand(3, 1.04f, 1.34f, 2.02f, 2.35f, 0.24f, 89.0f, 0.52f, 72.0f, 0.92f, 0.24f, 23.0f, 17.0f),
        }};
        break;
    case WaterPreset::Storm:
        result.isotropicFraction = 1.00f;
        result.couplingDriverFrequencyScale = 0.28f;
        result.crestBreakupStrength = 1.00f;
        result.crestBreakupFrequencyMultiplier = 1.95f;
        result.bands = {{
            makeSpectrumBand(5, 0.82f, 1.18f, 0.82f, 1.18f, 0.30f, 89.0f, 1.55f, 52.0f, 0.95f, 0.06f, 17.0f, 11.0f),
            makeSpectrumBand(4, 0.92f, 1.28f, 1.43f, 1.78f, 0.37f, 89.0f, 1.05f, 48.0f, 0.95f, 0.06f, 13.0f, 9.0f),
            makeSpectrumBand(3, 1.04f, 1.34f, 2.02f, 2.35f, 0.33f, 89.0f, 0.82f, 45.0f, 0.95f, 0.06f, 11.0f, 7.0f),
        }};
        break;
    case WaterPreset::Temperate:
    default:
        result.isotropicFraction = 0.80f;
        result.couplingDriverFrequencyScale = 0.32f;
        result.crestBreakupStrength = 0.94f;
        result.crestBreakupFrequencyMultiplier = 1.75f;
        result.bands = {{
            makeSpectrumBand(5, 0.82f, 1.18f, 0.82f, 1.18f, 0.34f, 86.0f, 1.35f, 62.0f, 0.95f, 0.10f, 23.0f, 17.0f),
            makeSpectrumBand(4, 0.92f, 1.28f, 1.43f, 1.78f, 0.36f, 89.0f, 0.90f, 58.0f, 0.95f, 0.10f, 17.0f, 13.0f),
            makeSpectrumBand(3, 1.04f, 1.34f, 2.02f, 2.35f, 0.30f, 89.0f, 0.68f, 54.0f, 0.95f, 0.10f, 13.0f, 9.0f),
        }};
        break;
    }
    return result;
}

class Pcg32 {
public:
    explicit Pcg32(std::uint32_t seed) {
        state_ = 0u;
        increment_ = (static_cast<std::uint64_t>(seed) << 1u) | 1u;
        next();
        state_ += 0x9E3779B97F4A7C15ull ^ seed;
        next();
    }

    std::uint32_t next() {
        const std::uint64_t oldState = state_;
        state_ = oldState * 6364136223846793005ull + increment_;
        const std::uint32_t xorshifted = static_cast<std::uint32_t>(((oldState >> 18u) ^ oldState) >> 27u);
        const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59u);
        return (xorshifted >> rotation) | (xorshifted << ((32u - rotation) & 31u));
    }

    float unitFloat() {
        return static_cast<float>(next() >> 8u) * (1.0f / 16777216.0f);
    }

private:
    std::uint64_t state_ = 0u;
    std::uint64_t increment_ = 1u;
};

float smootherstep(float edge0, float edge1, float x) {
    if (edge1 <= edge0) {
        return x > edge0 ? 1.0f : 0.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float softMinimumP8(float a, float b) {
    if (a <= 0.0f || b <= 0.0f) {
        return 0.0f;
    }
    const float scale = std::max(a, b);
    const float na = a / scale;
    const float nb = b / scale;
    return scale / std::pow(std::pow(na, -8.0f) + std::pow(nb, -8.0f), 1.0f / 8.0f);
}

QVector3D safeNormalized(const QVector3D& value, const QVector3D& fallback) {
    return value.lengthSquared() > 1e-10f ? value.normalized() : fallback;
}

void tangentFrame(const QVector3D& normal, QVector3D& tangent, QVector3D& bitangent) {
    const QVector3D helper = std::abs(normal.y()) < 0.9f
        ? QVector3D(0.0f, 1.0f, 0.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    tangent = QVector3D::crossProduct(helper, normal).normalized();
    bitangent = QVector3D::crossProduct(normal, tangent).normalized();
}

QVector3D sampleIsotropicAxis(Pcg32& random) {
    const float z = 1.0f - 2.0f * random.unitFloat();
    const float azimuth = 2.0f * kPi * random.unitFloat();
    const float radial = std::sqrt(std::max(1.0f - z * z, 0.0f));
    return QVector3D(radial * std::cos(azimuth), radial * std::sin(azimuth), z);
}

QVector3D sampleWindLobeAxis(
    Pcg32& random,
    const QVector3D& wind,
    float spreadDegrees) {
    QVector3D tangent;
    QVector3D bitangent;
    tangentFrame(wind, tangent, bitangent);
    const float maxAngle = std::clamp(spreadDegrees, 1.0f, 89.0f) * kPi / 180.0f;
    const float cosAngle = 1.0f - random.unitFloat() * (1.0f - std::cos(maxAngle));
    const float sinAngle = std::sqrt(std::max(1.0f - cosAngle * cosAngle, 0.0f));
    const float azimuth = 2.0f * kPi * random.unitFloat();
    return safeNormalized(
        wind * cosAngle + tangent * (sinAngle * std::cos(azimuth))
            + bitangent * (sinAngle * std::sin(azimuth)),
        wind);
}

float antipodalSeparation(const QVector3D& a, const QVector3D& b) {
    return std::acos(std::clamp(std::abs(QVector3D::dotProduct(a, b)), 0.0f, 1.0f));
}

QVector3D chooseSeparatedAxis(
    Pcg32& random,
    const QVector3D& wind,
    float spreadDegrees,
    bool isotropic,
    const WaterWaveSpectrum& spectrum,
    int usedCount) {
    QVector3D best = wind;
    float bestSeparation = -1.0f;
    constexpr int kCandidateCount = 128;
    for (int candidateIndex = 0; candidateIndex < kCandidateCount; ++candidateIndex) {
        const QVector3D candidate = isotropic
            ? sampleIsotropicAxis(random)
            : sampleWindLobeAxis(random, wind, spreadDegrees);
        float minimumSeparation = kPi;
        for (int i = 0; i < usedCount; ++i) {
            minimumSeparation = std::min(
                minimumSeparation,
                antipodalSeparation(candidate, spectrum.components[static_cast<std::size_t>(i)].axis));
        }
        if (minimumSeparation > bestSeparation) {
            bestSeparation = minimumSeparation;
            best = candidate;
        }
        if (minimumSeparation >= kMinimumAxisSeparationRadians && candidateIndex >= 24) {
            break;
        }
    }
    return best.normalized();
}

QVector3D chooseSeparatedPacketCenter(Pcg32& random, const WaterWaveSpectrum& spectrum, int usedCount) {
    QVector3D best(0.0f, 1.0f, 0.0f);
    float bestSeparation = -1.0f;
    for (int candidateIndex = 0; candidateIndex < 96; ++candidateIndex) {
        const QVector3D candidate = sampleIsotropicAxis(random);
        float minimumSeparation = kPi;
        for (int i = 0; i < usedCount; ++i) {
            minimumSeparation = std::min(minimumSeparation, std::acos(std::clamp(
                QVector3D::dotProduct(candidate, spectrum.components[static_cast<std::size_t>(i)].packetCenter),
                -1.0f,
                1.0f)));
        }
        if (minimumSeparation > bestSeparation) {
            bestSeparation = minimumSeparation;
            best = candidate;
        }
    }
    return best.normalized();
}

QVector3D rotateRodrigues(const QVector3D& value, const QVector3D& axis, double angle) {
    const float cosine = static_cast<float>(std::cos(angle));
    const float sine = static_cast<float>(std::sin(angle));
    return value * cosine
        + QVector3D::crossProduct(axis, value) * sine
        + axis * QVector3D::dotProduct(axis, value) * (1.0f - cosine);
}

float smootherstepDerivative(float edge0, float edge1, float x) {
    if (edge1 <= edge0 || x <= edge0 || x >= edge1) return 0.0f;
    const float t = (x - edge0) / (edge1 - edge0);
    return 30.0f * t * t * (t - 1.0f) * (t - 1.0f) / (edge1 - edge0);
}

float maximumSeaDepth(const HexSphereModel& model) {
    const float waterRadius = model.waterSurfaceRadius();
    float result = 0.0f;
    for (const Cell& cell : model.cells()) {
        if (cell.biome != Biome::Sea) {
            continue;
        }
        result = std::max(result, waterRadius - model.radiusForHeight(static_cast<float>(cell.height)));
    }
    return result;
}

float maximumBedAngularGradient(const HexSphereModel& model) {
    float result = 0.0f;
    const auto& cells = model.cells();
    for (const Cell& cell : cells) {
        const float radius = model.radiusForHeight(static_cast<float>(cell.height));
        for (int neighborId : cell.neighbors) {
            if (neighborId < 0 || neighborId >= static_cast<int>(cells.size())) continue;
            const Cell& neighbor = cells[static_cast<size_t>(neighborId)];
            const float angle = std::acos(std::clamp(
                QVector3D::dotProduct(cell.centroid.normalized(), neighbor.centroid.normalized()), -1.0f, 1.0f));
            const float neighborRadius = model.radiusForHeight(static_cast<float>(neighbor.height));
            result = std::max(result, std::abs(neighborRadius - radius) / std::max(angle, 1e-5f));
        }
    }
    return result;
}

int bandIndex(const WaterWaveComponent& component) {
    return std::clamp(static_cast<int>(component.band), 0, kWaterWaveBandCount - 1);
}

float quadraturePrimaryRmsGain(const WaterWaveSpectrum& spectrum) {
    constexpr int sampleCount = 2048;
    constexpr float goldenAngle = kPi * (3.0f - 2.2360679774997896964f);
    double squaredWeightMean = 0.0;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const float y = 1.0f - 2.0f
            * (static_cast<float>(sampleIndex) + 0.5f) / static_cast<float>(sampleCount);
        const float radial = std::sqrt(std::max(1.0f - y * y, 0.0f));
        const float azimuth = goldenAngle * static_cast<float>(sampleIndex);
        const QVector3D direction(radial * std::cos(azimuth), y, radial * std::sin(azimuth));
        const double sampleTime = 0.371 * static_cast<double>(sampleIndex);
        std::array<float, kWaterWaveComponentCount> packetSignals{};
        std::array<float, kWaterWaveBandCount> bandSignalSums{};
        for (int i = 0; i < kWaterWaveComponentCount; ++i) {
            const WaterWaveComponent& component = spectrum.components[static_cast<std::size_t>(i)];
            const double activityAngle = kPi * sampleTime
                / static_cast<double>(std::max(component.activityCycleSeconds, 1.0f))
                + static_cast<double>(component.activityPhase);
            const float sineValue = static_cast<float>(std::sin(std::remainder(activityAngle, kTwoPi)));
            const float activity = component.activityFloor
                + (1.0f - component.activityFloor) * sineValue * sineValue;
            const float envelope = smootherstep(
                component.packetOuterCosine,
                component.packetInnerCosine,
                QVector3D::dotProduct(direction, component.packetCenter));
            packetSignals[static_cast<std::size_t>(i)] = activity * envelope;
            bandSignalSums[static_cast<std::size_t>(bandIndex(component))] += component.weight
                * packetSignals[static_cast<std::size_t>(i)];
        }
        float primarySquaredWeight = 0.0f;
        for (int i = 0; i < kWaterWaveComponentCount; ++i) {
            const WaterWaveComponent& component = spectrum.components[static_cast<std::size_t>(i)];
            if (component.band != 0) continue;
            const float share = std::max(spectrum.bandShares[0], 1e-6f);
            const float meanSignal = bandSignalSums[0] / share;
            const float weight = component.weight * (1.0f + component.packetContrast
                * (packetSignals[static_cast<std::size_t>(i)] - meanSignal));
            primarySquaredWeight += weight * weight;
        }
        squaredWeightMean += primarySquaredWeight;
    }
    squaredWeightMean /= static_cast<double>(sampleCount);
    return std::sqrt(std::max(2.0f * static_cast<float>(squaredWeightMean), 1e-8f));
}

} // namespace

WaterSpectrumConfig defaultWaterSpectrumConfig() {
    return makeSpectrumConfig(WaterPreset::Temperate);
}

WaterSpectrumConfig waterSpectrumConfigForPreset(WaterPreset preset) {
    return makeSpectrumConfig(preset);
}

WaterWaveSpectrum buildWaterWaveSpectrum(const WaterSpectrumConfig& config, float octaveDetail) {
    WaterWaveSpectrum result;
    Pcg32 random(config.seed);
    const QVector3D wind = safeNormalized(config.windAxis, QVector3D(1.0f, 0.0f, 0.0f));
    const float detail = std::clamp(octaveDetail, 0.0f, 1.0f);
    int componentIndex = 0;

    for (int bandIndex = 0; bandIndex < static_cast<int>(config.bands.size()); ++bandIndex) {
        const WaterSpectrumBand& band = config.bands[static_cast<std::size_t>(bandIndex)];
        const int remaining = kWaterWaveComponentCount - componentIndex;
        const int modeCount = std::clamp(band.modeCount, 0, remaining);
        if (modeCount == 0) continue;
        const int bandStart = componentIndex;
        const int isotropicCount = std::clamp(
            static_cast<int>(std::lround(static_cast<float>(modeCount) * config.isotropicFraction)),
            0,
            modeCount);
        const float lowLogMinimum = std::log(std::max(band.lowDetailMinFrequencyMultiplier, 1e-4f));
        const float lowLogMaximum = std::log(std::max(
            band.lowDetailMaxFrequencyMultiplier, band.lowDetailMinFrequencyMultiplier + 1e-4f));
        const float highLogMinimum = std::log(std::max(band.minFrequencyMultiplier, 1e-4f));
        const float highLogMaximum = std::log(std::max(
            band.maxFrequencyMultiplier, band.minFrequencyMultiplier + 1e-4f));
        float jitterWeightSum = 0.0f;

        for (int localIndex = 0; localIndex < modeCount; ++localIndex) {
            WaterWaveComponent& component = result.components[static_cast<std::size_t>(componentIndex)];
            const bool isotropic = localIndex >= modeCount - isotropicCount;
            component.axis = chooseSeparatedAxis(
                random, wind, band.directionSpreadDegrees, isotropic, result, componentIndex);
            const QVector3D breakupCandidate = sampleIsotropicAxis(random);
            QVector3D breakupTangent = breakupCandidate
                - component.axis * QVector3D::dotProduct(breakupCandidate, component.axis);
            if (breakupTangent.lengthSquared() <= 1e-8f) {
                QVector3D fallbackBitangent;
                tangentFrame(component.axis, breakupTangent, fallbackBitangent);
            }
            component.breakupAxis = breakupTangent.normalized();
            const float jitter = 0.15f + 0.70f * random.unitFloat();
            const float stratum = (static_cast<float>(localIndex) + jitter) / static_cast<float>(modeCount);
            const float lowLogFrequency = lowLogMinimum + (lowLogMaximum - lowLogMinimum) * stratum;
            const float highLogFrequency = highLogMinimum + (highLogMaximum - highLogMinimum) * stratum;
            component.frequencyMultiplier = std::exp(
                lowLogFrequency + (highLogFrequency - lowLogFrequency) * detail);
            component.speedMultiplier = std::sqrt(component.frequencyMultiplier);
            component.phase = 2.0f * kPi * random.unitFloat();
            component.breakupFrequencyMultiplier = config.crestBreakupFrequencyMultiplier
                * (0.91f + 0.18f * random.unitFloat());
            component.breakupSpeedMultiplier = std::sqrt(component.breakupFrequencyMultiplier)
                * (0.93f + 0.14f * random.unitFloat());
            component.breakupPhase = 2.0f * kPi * random.unitFloat();
            component.weight = 0.93f + 0.14f * random.unitFloat();
            jitterWeightSum += component.weight;
            component.band = static_cast<std::uint8_t>(bandIndex);
            component.packetCenter = chooseSeparatedPacketCenter(random, result, componentIndex);
            QVector3D propagation = component.axis
                - component.packetCenter * QVector3D::dotProduct(component.packetCenter, component.axis);
            if (propagation.lengthSquared() <= 1e-8f) {
                QVector3D tangent;
                QVector3D bitangent;
                tangentFrame(component.packetCenter, tangent, bitangent);
                propagation = tangent;
            }
            propagation.normalize();
            component.packetOrbitAxis = safeNormalized(
                QVector3D::crossProduct(component.packetCenter, propagation),
                QVector3D(0.0f, 1.0f, 0.0f));
            component.phaseCouplingRadians = std::max(band.phaseCouplingRadians, 0.0f);
            component.packetInnerCosine = std::cos(
                std::clamp(band.packetInnerAngleDegrees, 0.0f, 179.0f) * kPi / 180.0f);
            component.packetOuterCosine = std::cos(
                std::clamp(band.packetOuterAngleDegrees, band.packetInnerAngleDegrees + 1.0f, 179.0f)
                * kPi / 180.0f);
            component.requestedPacketContrast = std::clamp(band.packetContrast, 0.0f, 0.95f);
            component.activityFloor = std::clamp(band.activityFloor, 0.0f, 1.0f);
            component.activityCycleSeconds = std::max(band.activityCycleSeconds, 1.0f);
            component.activityPhase = kPi * static_cast<float>(localIndex) / static_cast<float>(modeCount);
            const float mixingJitter = 0.88f + 0.24f * random.unitFloat();
            component.mixingRate = 2.0f * kPi * mixingJitter / std::max(band.mixingCycleSeconds, 1.0f);
            component.mixingPhase = 2.0f * kPi * random.unitFloat();
            component.couplingPhaseOffsets[0] = 2.0f * kPi * random.unitFloat();
            component.couplingPhaseOffsets[1] = 2.0f * kPi * random.unitFloat();
            ++componentIndex;
        }

        const float bandShare = std::max(band.energy, 0.0f);
        result.bandShares[static_cast<std::size_t>(bandIndex)] = bandShare;
        float maximumBaseWeight = 0.0f;
        for (int i = bandStart; i < componentIndex; ++i) {
            WaterWaveComponent& component = result.components[static_cast<std::size_t>(i)];
            component.weight = bandShare * component.weight / std::max(jitterWeightSum, 1e-6f);
            maximumBaseWeight = std::max(maximumBaseWeight, component.weight);
        }
        const float contrastCap = maximumBaseWeight > 1e-6f
            ? std::max(kMaximumLocalModeWeight / maximumBaseWeight - 1.0f, 0.0f)
            : 0.0f;
        const float effectiveContrast = std::clamp(band.packetContrast, 0.0f, std::min(contrastCap, 0.95f));
        for (int i = bandStart; i < componentIndex; ++i) {
            result.components[static_cast<std::size_t>(i)].packetContrast = effectiveContrast;
        }
    }

    float totalBandShare = 0.0f;
    for (float share : result.bandShares) totalBandShare += share;
    if (totalBandShare <= std::numeric_limits<float>::epsilon()) return result;
    for (float& share : result.bandShares) share /= totalBandShare;
    for (WaterWaveComponent& component : result.components) component.weight /= totalBandShare;

    // Four unwarped modes drive all cross-phase modulation. Keeping this graph
    // acyclic preserves the exact zero mean and diagonal phase variance.
    result.driverComponentIndices = {{ 0u, 5u, 8u, 9u }};
    for (int componentIndexValue = 0; componentIndexValue < kWaterWaveComponentCount; ++componentIndexValue) {
        WaterWaveComponent& component = result.components[static_cast<std::size_t>(componentIndexValue)];
        int ownDriverSlot = -1;
        for (int driverSlot = 0; driverSlot < kWaterWaveDriverCount; ++driverSlot) {
            if (result.driverComponentIndices[static_cast<std::size_t>(driverSlot)] == componentIndexValue) {
                ownDriverSlot = driverSlot;
                break;
            }
        }
        if (ownDriverSlot >= 0) {
            component.phaseCouplingRadians = 0.0f;
            continue;
        }
        const int firstSlot = (componentIndexValue + bandIndex(component)) % kWaterWaveDriverCount;
        int secondSlot = (componentIndexValue * 3 + 1) % kWaterWaveDriverCount;
        if (secondSlot == firstSlot) secondSlot = (secondSlot + 1) % kWaterWaveDriverCount;
        component.driverSlots = {{
            static_cast<std::int8_t>(firstSlot),
            static_cast<std::int8_t>(secondSlot),
        }};
    }

    for (WaterWaveComponent& component : result.components) {
        if (component.weight <= 1e-6f) continue;
        ++result.activeModeCount;
        result.maximumWeight = std::max(result.maximumWeight, component.weight);
        result.requestedMaximumPacketContrast = std::max(
            result.requestedMaximumPacketContrast, component.requestedPacketContrast);
        result.maximumPacketContrast = std::max(result.maximumPacketContrast, component.packetContrast);
        result.maximumCouplingRadians = std::max(
            result.maximumCouplingRadians, component.phaseCouplingRadians);
    }
    result.primaryRmsGain = quadraturePrimaryRmsGain(result);
    result.crestBreakupStrength = std::clamp(config.crestBreakupStrength, 0.0f, 1.0f);
    return result;
}

SphericalPrimaryWaveScale deriveSphericalPrimaryWaveScale(
    float relativeHeightPerArea,
    float wavesPerCell,
    float waterRadius,
    std::size_t cellCount) {
    SphericalPrimaryWaveScale result;
    const float radius = std::max(waterRadius, 1e-5f);
    const float count = static_cast<float>(std::max<std::size_t>(cellCount, 1u));
    result.relativeHeightPerArea = std::clamp(relativeHeightPerArea, 0.05f, 0.80f);
    result.wavesPerCell = std::clamp(wavesPerCell, 1.0f, 30.0f);
    result.averageCellArea = 4.0f * kPi * radius * radius / count;
    result.waveArea = result.averageCellArea / result.wavesPerCell;
    result.fullHeight = result.relativeHeightPerArea * result.waveArea / radius;
    result.wavelength = std::sqrt(std::max(result.waveArea, 1e-10f));
    result.angularFrequency = 2.0f * kPi / result.wavelength;
    return result;
}

BoundedWaterWaveShape resolveBoundedWaterWaveShape(float sharpness) {
    BoundedWaterWaveShape result;
    const float normalizedSharpness = std::clamp(sharpness, 0.0f, 1.0f);
    result.exponent = 1.0f + 1.8f * normalizedSharpness;
    const double exponent = static_cast<double>(result.exponent);
    result.mean = static_cast<float>(std::exp(-exponent) * std::cyl_bessel_i(0.0, exponent));
    const float troughValue = std::exp(-2.0f * result.exponent);
    result.troughRatio = (result.mean - troughValue) / (1.0f - result.mean);
    const float y = (std::sqrt(1.0f + 4.0f * result.exponent * result.exponent) - 1.0f)
        / (2.0f * result.exponent);
    result.maximumDerivative = result.exponent
        * std::exp(result.exponent * (y - 1.0f))
        * std::sqrt(std::max(1.0f - y * y, 0.0f))
        / (1.0f - result.mean);
    return result;
}

WaterWaveInteractionShape resolveWaterWaveInteractionShape(
    const BoundedWaterWaveShape& carrier,
    float crestSharpness,
    const WaterWaveSpectrum& spectrum) {
    WaterWaveInteractionShape result;
    const double exponent = static_cast<double>(carrier.exponent);
    const double mean = static_cast<double>(carrier.mean);
    const float sharpness = std::clamp(crestSharpness, 0.0f, 1.0f);
    result.strength = 0.35f * sharpness;
    result.thirdMomentStrength = 0.55f * sharpness;
    const double secondRawMoment = std::exp(-2.0 * exponent) * std::cyl_bessel_i(0.0, 2.0 * exponent);
    const double thirdRawMoment = std::exp(-3.0 * exponent) * std::cyl_bessel_i(0.0, 3.0 * exponent);
    const float baseVariance = static_cast<float>(
        (secondRawMoment - mean * mean)
        / ((1.0 - mean) * (1.0 - mean)));
    const float baseThirdMoment = static_cast<float>(
        (thirdRawMoment - 3.0 * mean * secondRawMoment + 2.0 * mean * mean * mean)
        / ((1.0 - mean) * (1.0 - mean) * (1.0 - mean)));
    const float breakup = spectrum.crestBreakupStrength;
    const float baseShare = 1.0f - breakup;
    result.carrierVariance = baseVariance
        * (baseShare * baseShare + breakup * breakup * baseVariance);
    result.carrierThirdMoment = baseThirdMoment
        * (baseShare * baseShare * baseShare
            + 3.0f * baseShare * breakup * breakup * baseVariance
            + breakup * breakup * breakup * baseThirdMoment);
    float maximumSquaredWeightSum = 0.0f;
    float maximumCubedWeightSum = 0.0f;
    for (float bandShare : spectrum.bandShares) {
        maximumSquaredWeightSum += bandShare * bandShare;
        maximumCubedWeightSum += bandShare * bandShare * bandShare;
    }
    result.fieldVariance = result.carrierVariance * maximumSquaredWeightSum;
    result.fieldThirdMoment = result.carrierThirdMoment * maximumCubedWeightSum;
    result.crestScale = 1.0f
        + result.strength * (1.0f - result.fieldVariance)
        + result.thirdMomentStrength * (1.0f - result.fieldThirdMoment);
    const float minimumCarrier = -carrier.troughRatio;
    float minimumHeight = 1.0f;
    for (float variance : { 0.0f, result.fieldVariance }) {
        for (float thirdMoment : { 0.0f, result.fieldThirdMoment }) {
            const float denominator = 1.0f
                + result.strength * (1.0f - variance)
                + result.thirdMomentStrength * (1.0f - thirdMoment);
            const float numerator = minimumCarrier
                + result.strength * (minimumCarrier * minimumCarrier - variance)
                + result.thirdMomentStrength
                    * (minimumCarrier * minimumCarrier * minimumCarrier - thirdMoment);
            minimumHeight = std::min(minimumHeight, numerator / denominator);
        }
    }
    result.troughRatio = std::max(-minimumHeight, 1e-5f);
    result.maximumDerivative = 1.0f + 2.0f * result.strength
        + 3.0f * result.thirdMomentStrength;
    return result;
}

WaterWaveSpec makeWaterWaveSpec(const WaterParams& params, const HexSphereModel& model) {
    return makeWaterWaveSpec(params, model, waterSpectrumConfigForPreset(params.preset));
}

WaterWaveSpec makeWaterWaveSpec(
    const WaterParams& params,
    const HexSphereModel& model,
    const WaterSpectrumConfig& spectrumConfig) {
    WaterWaveSpec result;
    const float waterRadius = std::max(model.waterSurfaceRadius(), 1e-5f);
    const SphericalPrimaryWaveScale scale = deriveSphericalPrimaryWaveScale(
        params.shellWaveAmplitude, params.shellWaveFrequency, waterRadius, model.cells().size());
    result.relativeHeightPerArea = scale.relativeHeightPerArea;
    result.primaryWavesPerCell = scale.wavesPerCell;
    result.averageCellArea = scale.averageCellArea;
    result.primaryWaveArea = scale.waveArea;
    result.primaryWaveHeight = scale.fullHeight;
    result.requestedFrequency = scale.angularFrequency;
    // The transverse wavelet factor now prevents a sharp carrier from
    // surviving as one global stripe, so geometric curvature can be restored.
    result.shape = resolveBoundedWaterWaveShape(std::min(params.waveStrength, 0.72f));
    result.octaveDetail = std::clamp(params.octaveDetail, 0.0f, 1.0f);
    result.speed = std::max(params.shellWaveSpeed, 0.0f);

    const WaterWaveSpectrum spectrum = buildWaterWaveSpectrum(spectrumConfig, result.octaveDetail);
    result.components = spectrum.components;
    result.activeModeCount = spectrum.activeModeCount;
    result.maximumModeWeight = spectrum.maximumWeight;
    result.requestedMaximumPacketContrast = spectrum.requestedMaximumPacketContrast;
    result.maximumPacketContrast = spectrum.maximumPacketContrast;
    result.maximumCouplingRadians = spectrum.maximumCouplingRadians;
    result.crestBreakupStrength = spectrum.crestBreakupStrength;
    result.primaryRmsGain = spectrum.primaryRmsGain;
    result.groupVelocityRatio = spectrumConfig.groupVelocityRatio;
    result.couplingDriverFrequencyScale = spectrumConfig.couplingDriverFrequencyScale;
    result.driverComponentIndices = spectrum.driverComponentIndices;
    result.bandShares = spectrum.bandShares;
    result.interaction = resolveWaterWaveInteractionShape(result.shape, params.waveStrength, spectrum);
    const double shapeExponent = static_cast<double>(result.shape.exponent);
    const double shapeMean = static_cast<double>(result.shape.mean);
    const double shapeSecondRawMoment = std::exp(-2.0 * shapeExponent)
        * std::cyl_bessel_i(0.0, 2.0 * shapeExponent);
    const float baseCarrierVariance = static_cast<float>(
        (shapeSecondRawMoment - shapeMean * shapeMean)
        / ((1.0 - shapeMean) * (1.0 - shapeMean)));
    const float breakupBaseShare = 1.0f - result.crestBreakupStrength;
    const float breakupRmsFactor = std::sqrt(std::max(
        breakupBaseShare * breakupBaseShare
            + result.crestBreakupStrength * result.crestBreakupStrength * baseCarrierVariance,
        1e-5f));
    // Cutting a long carrier into compact wavelets removes RMS energy. Restore
    // that energy through the already bounded geometric amplitude, not through
    // an unbounded shader gain.
    result.primaryRmsGain *= breakupRmsFactor;

    const float averageCellWidth = model.cells().empty()
        ? model.waterSurfaceRadius() * 0.05f
        : model.waterSurfaceRadius() * std::sqrt(4.0f * kPi / static_cast<float>(model.cells().size()));
    result.shoreFadeWorldWidth = std::max(
        averageCellWidth * (0.08f + 0.02f * static_cast<float>(std::max(params.beachWidth - 1, 0))),
        1e-5f);

    const float primaryCrestAmplitude = result.primaryWaveHeight
        / (1.0f + result.interaction.troughRatio);
    result.requestedAmplitude = primaryCrestAmplitude / std::max(result.primaryRmsGain, 1e-5f);
    return result;
}

ResolvedWaterWaveSpec resolveWaterWaveSpec(const WaterWaveSpec& spec, const WaterParams& params, const HexSphereModel& model) {
    ResolvedWaterWaveSpec result;
    result.requested = spec;
    const float r0 = model.waterSurfaceRadius();
    result.waterSurfaceRadius = r0;
    result.maximumSeaDepth = maximumSeaDepth(model);

    result.bounds.bottomEpsilon = std::max(1e-4f * r0, 0.02f * model.heightStep());
    result.bounds.boundEpsilon = std::max(1e-4f * r0, 2e-5f);
    const float troughRatio = spec.interaction.troughRatio;
    const float depthAmplitudeCap = std::max(result.maximumSeaDepth - result.bounds.bottomEpsilon, 0.0f)
        / troughRatio;
    result.effectiveGeometryAmplitude = std::min(spec.requestedAmplitude, depthAmplitudeCap);

    const float shellThickness = (1.0f + troughRatio) * result.effectiveGeometryAmplitude
        + 2.0f * result.bounds.boundEpsilon;
    const float innerEstimate = std::max(
        r0 - troughRatio * result.effectiveGeometryAmplitude, r0 * 0.25f);
    const float worstAngularPath = std::sqrt(std::max(2.0f * shellThickness / innerEstimate, 1e-8f));
    float maximumMultiplier = 1.0f;
    std::array<float, kWaterWaveBandCount> maximumEnvelopeDerivative{};
    std::array<float, kWaterWaveBandCount> maximumBandContrast{};
    for (const WaterWaveComponent& component : spec.components) {
        if (component.weight > 1e-6f) {
            float effectiveMultiplier = component.frequencyMultiplier;
            if (component.driverSlots[0] >= 0 && component.driverSlots[1] >= 0) {
                const int driverIndex0 = spec.driverComponentIndices[
                    static_cast<std::size_t>(component.driverSlots[0])];
                const int driverIndex1 = spec.driverComponentIndices[
                    static_cast<std::size_t>(component.driverSlots[1])];
                effectiveMultiplier += component.phaseCouplingRadians * kInverseSqrtTwo
                    * spec.couplingDriverFrequencyScale
                    * (spec.components[static_cast<std::size_t>(driverIndex0)].frequencyMultiplier
                        + spec.components[static_cast<std::size_t>(driverIndex1)].frequencyMultiplier);
            }
            maximumMultiplier = std::max(maximumMultiplier, std::max(
                effectiveMultiplier, component.breakupFrequencyMultiplier));
            const int band = bandIndex(component);
            const float envelopeDerivative = 1.875f / std::max(
                component.packetInnerCosine - component.packetOuterCosine, 1e-5f);
            maximumEnvelopeDerivative[static_cast<std::size_t>(band)] = std::max(
                maximumEnvelopeDerivative[static_cast<std::size_t>(band)], envelopeDerivative);
            maximumBandContrast[static_cast<std::size_t>(band)] = std::max(
                maximumBandContrast[static_cast<std::size_t>(band)], component.packetContrast);
        }
    }
    const float frequencyBudget = (static_cast<float>(result.maximumScanSteps - 2) * kPhasePerScanSegment)
        / std::max(worstAngularPath * r0 * maximumMultiplier, 1e-6f);
    result.effectiveGeometryFrequency = std::min(spec.requestedFrequency, frequencyBudget);
    // Like the reference shader's 12 height / 36 normal iterations, two
    // additional analytic octaves are evaluated only once at the resolved hit.
    result.microFrequency = result.effectiveGeometryFrequency * 3.55f;
    result.microNormalStrength = 0.72f * spec.octaveDetail;
    result.microDrag = 0.0f;
    result.crestSharpness = std::clamp(params.waveStrength, 0.0f, 1.0f);
    result.maximumPhaseGradient = result.effectiveGeometryFrequency * r0 * maximumMultiplier;
    float weightGradientBound = 0.0f;
    for (int band = 0; band < kWaterWaveBandCount; ++band) {
        weightGradientBound += 2.0f
            * maximumBandContrast[static_cast<std::size_t>(band)]
            * spec.bandShares[static_cast<std::size_t>(band)]
            * maximumEnvelopeDerivative[static_cast<std::size_t>(band)];
    }
    const float breakupGradientBound = result.effectiveGeometryFrequency * r0
        * maximumMultiplier;
    const float carrierGradientBound = spec.shape.maximumDerivative
        * (result.maximumPhaseGradient
            + spec.crestBreakupStrength * breakupGradientBound)
        + weightGradientBound;
    const float varianceGradientBound = 2.0f
        * spec.interaction.carrierVariance * weightGradientBound;
    const float thirdMomentGradientBound = 3.0f
        * std::abs(spec.interaction.carrierThirdMoment) * weightGradientBound;
    const float collectiveCarrierDerivative = 1.0f
        + 2.0f * spec.interaction.strength
        + 3.0f * spec.interaction.thirdMomentStrength;
    const float collectiveVarianceDerivative = spec.interaction.strength
        * (1.0f + spec.interaction.troughRatio);
    const float collectiveThirdMomentDerivative = spec.interaction.thirdMomentStrength
        * (1.0f + spec.interaction.troughRatio);
    result.maximumWaveAngularGradient = collectiveCarrierDerivative * carrierGradientBound
        + collectiveVarianceDerivative * varianceGradientBound
        + collectiveThirdMomentDerivative * thirdMomentGradientBound;
    const float shoreAmplitudeGradient = result.effectiveGeometryAmplitude * 1.875f * r0
        / std::max(spec.shoreFadeWorldWidth, 1e-5f);
    const float bedAmplitudeGradient = maximumBedAngularGradient(model) / troughRatio;
    result.maximumAmplitudeAngularGradient = shoreAmplitudeGradient + bedAmplitudeGradient;

    result.bounds.outerRadius = r0 + result.effectiveGeometryAmplitude + 2.0f * result.bounds.boundEpsilon;
    result.bounds.innerRadius = r0 - troughRatio * result.effectiveGeometryAmplitude
        - 2.0f * result.bounds.boundEpsilon;
    return result;
}

float boundedWaterWave(float phase) {
    return boundedWaterWave(phase, BoundedWaterWaveShape{});
}

float boundedWaterWave(float phase, const BoundedWaterWaveShape& shape) {
    const float g = std::exp(shape.exponent * (std::sin(phase) - 1.0f));
    return (g - shape.mean) / (1.0f - shape.mean);
}

WaterWaveFrameState resolveWaterWaveFrameState(const ResolvedWaterWaveSpec& spec, double time) {
    WaterWaveFrameState result;
    const double frequencyRadius = static_cast<double>(spec.effectiveGeometryFrequency)
        * static_cast<double>(spec.waterSurfaceRadius);
    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& component = spec.requested.components[static_cast<std::size_t>(i)];
        const double temporalPhase = static_cast<double>(component.phase)
            - static_cast<double>(spec.requested.speed)
                * static_cast<double>(component.speedMultiplier) * time;
        result.temporalPhases[static_cast<std::size_t>(i)] = static_cast<float>(
            std::remainder(temporalPhase, kTwoPi));
        const double breakupTemporalPhase = static_cast<double>(component.breakupPhase)
            - static_cast<double>(spec.requested.speed)
                * static_cast<double>(component.breakupSpeedMultiplier) * time;
        result.breakupTemporalPhases[static_cast<std::size_t>(i)] = static_cast<float>(
            std::remainder(breakupTemporalPhase, kTwoPi));
        constexpr std::array<double, 4> microScales{{
            2.05, 2.05 * 1.071, 3.55, 3.55 * 0.937
        }};
        constexpr std::array<double, 4> breakupMix{{ 0.73, -0.73, 0.61, -1.0 / 0.61 }};
        for (int microMode = 0; microMode < 4; ++microMode) {
            const double phaseSeed = static_cast<double>(component.phase)
                + breakupMix[static_cast<std::size_t>(microMode)]
                    * static_cast<double>(component.breakupPhase)
                + static_cast<double>(i + 1) * (2.39996322972865332 + 0.47 * microMode);
            const double octaveSpeed = static_cast<double>(spec.requested.speed)
                * std::sqrt(static_cast<double>(component.frequencyMultiplier)
                    * microScales[static_cast<std::size_t>(microMode)]);
            result.microTemporalPhases[static_cast<std::size_t>(i)][static_cast<std::size_t>(microMode)]
                = static_cast<float>(std::remainder(phaseSeed - octaveSpeed * time, kTwoPi));
        }

        double groupAngularVelocity = 0.0;
        if (frequencyRadius > 1e-10 && component.frequencyMultiplier > 1e-6f) {
            groupAngularVelocity = static_cast<double>(spec.requested.groupVelocityRatio)
                * static_cast<double>(spec.requested.speed)
                * static_cast<double>(component.speedMultiplier)
                / (frequencyRadius * static_cast<double>(component.frequencyMultiplier));
        }
        const double packetAngle = std::remainder(groupAngularVelocity * time, kTwoPi);
        result.packetCenters[static_cast<std::size_t>(i)] = safeNormalized(
            rotateRodrigues(component.packetCenter, component.packetOrbitAxis, packetAngle),
            component.packetCenter);

        const double activityAngle = kPi * time
            / static_cast<double>(std::max(component.activityCycleSeconds, 1.0f))
            + static_cast<double>(component.activityPhase);
        const float activitySine = static_cast<float>(std::sin(std::remainder(activityAngle, kTwoPi)));
        result.activities[static_cast<std::size_t>(i)] = component.activityFloor
            + (1.0f - component.activityFloor) * activitySine * activitySine;

        const double mixingAngle = static_cast<double>(component.mixingRate) * time
            + static_cast<double>(component.mixingPhase);
        result.coupling0[static_cast<std::size_t>(i)] = component.phaseCouplingRadians
            * kInverseSqrtTwo * static_cast<float>(std::cos(std::remainder(mixingAngle, kTwoPi)));
        result.coupling1[static_cast<std::size_t>(i)] = component.phaseCouplingRadians
            * kInverseSqrtTwo * static_cast<float>(std::sin(std::remainder(mixingAngle, kTwoPi)));
    }
    for (int driverSlot = 0; driverSlot < kWaterWaveDriverCount; ++driverSlot) {
        const int driverIndex = spec.requested.driverComponentIndices[static_cast<std::size_t>(driverSlot)];
        const WaterWaveComponent& driver = spec.requested.components[static_cast<std::size_t>(driverIndex)];
        const double driverPhase = static_cast<double>(spec.requested.couplingDriverFrequencyScale)
            * (static_cast<double>(driver.phase)
                - static_cast<double>(spec.requested.speed)
                    * static_cast<double>(driver.speedMultiplier) * time);
        result.driverTemporalPhases[static_cast<std::size_t>(driverSlot)] = static_cast<float>(
            std::remainder(driverPhase, kTwoPi));
    }
    return result;
}

WaterWaveFieldSample sampleWaterWaveFieldWithGradient(
    const ResolvedWaterWaveSpec& spec,
    const QVector3D& direction,
    double time) {
    WaterWaveFieldSample result;
    const QVector3D dir = safeNormalized(direction, QVector3D(0.0f, 1.0f, 0.0f));
    const WaterWaveFrameState frame = resolveWaterWaveFrameState(spec, time);
    std::array<float, kWaterWaveComponentCount> basePhases{};
    std::array<QVector3D, kWaterWaveComponentCount> baseGradients{};
    std::array<float, kWaterWaveComponentCount> breakupPhases{};
    std::array<QVector3D, kWaterWaveComponentCount> breakupGradients{};
    std::array<float, kWaterWaveComponentCount> packetSignals{};
    std::array<QVector3D, kWaterWaveComponentCount> signalGradients{};
    std::array<float, kWaterWaveComponentCount> localWeights{};
    std::array<QVector3D, kWaterWaveComponentCount> weightGradients{};
    std::array<float, kWaterWaveBandCount> bandSignalSums{};
    std::array<QVector3D, kWaterWaveBandCount> bandSignalGradients{};

    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& component = spec.requested.components[static_cast<std::size_t>(i)];
        const float frequencyRadius = spec.effectiveGeometryFrequency
            * component.frequencyMultiplier * spec.waterSurfaceRadius;
        const float axisDot = QVector3D::dotProduct(dir, component.axis);
        basePhases[static_cast<std::size_t>(i)] = static_cast<float>(std::remainder(
            static_cast<double>(frequencyRadius * axisDot)
                + static_cast<double>(frame.temporalPhases[static_cast<std::size_t>(i)]),
            kTwoPi));
        baseGradients[static_cast<std::size_t>(i)] = frequencyRadius
            * (component.axis - dir * axisDot);
        const float breakupFrequencyRadius = spec.effectiveGeometryFrequency
            * component.breakupFrequencyMultiplier * spec.waterSurfaceRadius;
        const float breakupDot = QVector3D::dotProduct(dir, component.breakupAxis);
        breakupPhases[static_cast<std::size_t>(i)] = static_cast<float>(std::remainder(
            static_cast<double>(breakupFrequencyRadius * breakupDot)
                + static_cast<double>(frame.breakupTemporalPhases[static_cast<std::size_t>(i)]),
            kTwoPi));
        breakupGradients[static_cast<std::size_t>(i)] = breakupFrequencyRadius
            * (component.breakupAxis - dir * breakupDot);

        const QVector3D& packetCenter = frame.packetCenters[static_cast<std::size_t>(i)];
        const float packetDot = QVector3D::dotProduct(dir, packetCenter);
        const float envelope = smootherstep(
            component.packetOuterCosine, component.packetInnerCosine, packetDot);
        const float activity = frame.activities[static_cast<std::size_t>(i)];
        packetSignals[static_cast<std::size_t>(i)] = activity * envelope;
        signalGradients[static_cast<std::size_t>(i)] = activity
            * smootherstepDerivative(component.packetOuterCosine, component.packetInnerCosine, packetDot)
            * (packetCenter - dir * packetDot);
        const int band = bandIndex(component);
        bandSignalSums[static_cast<std::size_t>(band)] += component.weight
            * packetSignals[static_cast<std::size_t>(i)];
        bandSignalGradients[static_cast<std::size_t>(band)] += component.weight
            * signalGradients[static_cast<std::size_t>(i)];
    }

    std::array<float, kWaterWaveBandCount> meanSignals{};
    std::array<QVector3D, kWaterWaveBandCount> meanSignalGradients{};
    for (int band = 0; band < kWaterWaveBandCount; ++band) {
        const float share = std::max(spec.requested.bandShares[static_cast<std::size_t>(band)], 1e-6f);
        meanSignals[static_cast<std::size_t>(band)] = bandSignalSums[static_cast<std::size_t>(band)] / share;
        meanSignalGradients[static_cast<std::size_t>(band)] = bandSignalGradients[static_cast<std::size_t>(band)] / share;
    }

    std::array<float, kWaterWaveDriverCount> driverControlPhases{};
    std::array<QVector3D, kWaterWaveDriverCount> driverControlGradients{};
    for (int driverSlot = 0; driverSlot < kWaterWaveDriverCount; ++driverSlot) {
        const int driverIndex = spec.requested.driverComponentIndices[static_cast<std::size_t>(driverSlot)];
        const WaterWaveComponent& driver = spec.requested.components[static_cast<std::size_t>(driverIndex)];
        const float driverFrequencyRadius = spec.requested.couplingDriverFrequencyScale
            * spec.effectiveGeometryFrequency * driver.frequencyMultiplier * spec.waterSurfaceRadius;
        const float driverDot = QVector3D::dotProduct(dir, driver.axis);
        driverControlPhases[static_cast<std::size_t>(driverSlot)] = static_cast<float>(std::remainder(
            static_cast<double>(driverFrequencyRadius * driverDot)
                + static_cast<double>(frame.driverTemporalPhases[static_cast<std::size_t>(driverSlot)]),
            kTwoPi));
        driverControlGradients[static_cast<std::size_t>(driverSlot)] = driverFrequencyRadius
            * (driver.axis - dir * driverDot);
    }

    float squaredWeightSum = 0.0f;
    float cubedWeightSum = 0.0f;
    QVector3D varianceGradient;
    QVector3D thirdMomentGradient;
    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& component = spec.requested.components[static_cast<std::size_t>(i)];
        const int band = bandIndex(component);
        const float weight = component.weight * (1.0f + component.packetContrast
            * (packetSignals[static_cast<std::size_t>(i)] - meanSignals[static_cast<std::size_t>(band)]));
        const QVector3D weightGradient = component.weight * component.packetContrast
            * (signalGradients[static_cast<std::size_t>(i)]
                - meanSignalGradients[static_cast<std::size_t>(band)]);
        localWeights[static_cast<std::size_t>(i)] = weight;
        weightGradients[static_cast<std::size_t>(i)] = weightGradient;
        squaredWeightSum += weight * weight;
        cubedWeightSum += weight * weight * weight;
        varianceGradient += 2.0f * spec.requested.interaction.carrierVariance
            * weight * weightGradient;
        thirdMomentGradient += 3.0f * spec.requested.interaction.carrierThirdMoment
            * weight * weight * weightGradient;
        result.maximumLocalWeight = std::max(result.maximumLocalWeight, weight);
        result.packetSignal += weight * packetSignals[static_cast<std::size_t>(i)];
    }
    result.fieldVariance = spec.requested.interaction.carrierVariance * squaredWeightSum;
    const float fieldThirdMoment = spec.requested.interaction.carrierThirdMoment * cubedWeightSum;
    result.effectiveModeCount = 1.0f / std::max(squaredWeightSum, 1e-6f);

    QVector3D carrierGradient;
    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& component = spec.requested.components[static_cast<std::size_t>(i)];
        float phase = basePhases[static_cast<std::size_t>(i)];
        QVector3D phaseGradient = baseGradients[static_cast<std::size_t>(i)];
        float displacement = 0.0f;
        if (component.driverSlots[0] >= 0 && component.driverSlots[1] >= 0) {
            const int driverSlot0 = component.driverSlots[0];
            const int driverSlot1 = component.driverSlots[1];
            const float argument0 = driverControlPhases[static_cast<std::size_t>(driverSlot0)]
                + component.couplingPhaseOffsets[0];
            const float argument1 = driverControlPhases[static_cast<std::size_t>(driverSlot1)]
                + component.couplingPhaseOffsets[1];
            const float coefficient0 = frame.coupling0[static_cast<std::size_t>(i)];
            const float coefficient1 = frame.coupling1[static_cast<std::size_t>(i)];
            displacement = coefficient0 * std::sin(argument0) + coefficient1 * std::sin(argument1);
            phase += displacement;
            phaseGradient += coefficient0 * std::cos(argument0)
                    * driverControlGradients[static_cast<std::size_t>(driverSlot0)]
                + coefficient1 * std::cos(argument1)
                    * driverControlGradients[static_cast<std::size_t>(driverSlot1)];
        }
        const float exponential = std::exp(spec.requested.shape.exponent * (std::sin(phase) - 1.0f));
        const float carrier = (exponential - spec.requested.shape.mean) / (1.0f - spec.requested.shape.mean);
        const float derivative = spec.requested.shape.exponent * exponential * std::cos(phase)
            / (1.0f - spec.requested.shape.mean);
        const float breakupPhase = breakupPhases[static_cast<std::size_t>(i)];
        const float breakupExponential = std::exp(
            spec.requested.shape.exponent * (std::sin(breakupPhase) - 1.0f));
        const float breakupCarrier = (breakupExponential - spec.requested.shape.mean)
            / (1.0f - spec.requested.shape.mean);
        const float breakupDerivative = spec.requested.shape.exponent * breakupExponential
            * std::cos(breakupPhase) / (1.0f - spec.requested.shape.mean);
        const float breakupFactor = 1.0f + spec.requested.crestBreakupStrength
            * (breakupCarrier - 1.0f);
        const float wavelet = carrier * breakupFactor;
        const QVector3D waveletGradient = derivative * breakupFactor * phaseGradient
            + carrier * spec.requested.crestBreakupStrength * breakupDerivative
                * breakupGradients[static_cast<std::size_t>(i)];
        const float weight = localWeights[static_cast<std::size_t>(i)];
        result.carrierHeight += weight * wavelet;
        carrierGradient += weight * waveletGradient
            + wavelet * weightGradients[static_cast<std::size_t>(i)];
        result.phaseDisplacement += weight * std::abs(displacement);
    }

    const WaterWaveInteractionShape& interaction = spec.requested.interaction;
    const float denominator = 1.0f
        + interaction.strength * (1.0f - result.fieldVariance)
        + interaction.thirdMomentStrength * (1.0f - fieldThirdMoment);
    const float numerator = result.carrierHeight + interaction.strength
            * (result.carrierHeight * result.carrierHeight - result.fieldVariance)
        + interaction.thirdMomentStrength
            * (result.carrierHeight * result.carrierHeight * result.carrierHeight - fieldThirdMoment);
    result.height = numerator / denominator;
    const float carrierPartial = (1.0f + 2.0f * interaction.strength * result.carrierHeight)
        / denominator;
    const float variancePartial = interaction.strength * (result.height - 1.0f) / denominator;
    const float thirdMomentPartial = interaction.thirdMomentStrength
        * (result.height - 1.0f) / denominator;
    const float carrierPartialCubic = 3.0f * interaction.thirdMomentStrength
        * result.carrierHeight * result.carrierHeight / denominator;
    result.angularGradient = (carrierPartial + carrierPartialCubic) * carrierGradient
        + variancePartial * varianceGradient
        + thirdMomentPartial * thirdMomentGradient;
    return result;
}

float sampleWaterWaveField(const ResolvedWaterWaveSpec& spec, const QVector3D& direction, double time) {
    return sampleWaterWaveFieldWithGradient(spec, direction, time).height;
}

float depthCappedWaterAmplitude(const ResolvedWaterWaveSpec& spec, float terrainRadius, float signedShoreDistance, float waterRadius) {
    const float shoreMask = smootherstep(0.0f, spec.requested.shoreFadeWorldWidth, signedShoreDistance);
    const float requestedLocal = spec.effectiveGeometryAmplitude * shoreMask;
    const float availableDepth = std::max(waterRadius - terrainRadius - spec.bounds.bottomEpsilon, 0.0f);
    const float depthCap = availableDepth / spec.requested.interaction.troughRatio;
    return softMinimumP8(requestedLocal, depthCap);
}

float sampleWaterSurfaceRadius(
    const ResolvedWaterWaveSpec& spec,
    const QVector3D& direction,
    double time,
    float terrainRadius,
    float signedShoreDistance,
    float waterRadius) {
    const float amplitude = depthCappedWaterAmplitude(spec, terrainRadius, signedShoreDistance, waterRadius);
    return waterRadius + amplitude * sampleWaterWaveField(spec, direction, time);
}
