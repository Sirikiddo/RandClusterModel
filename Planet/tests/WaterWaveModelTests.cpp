#include "tests/WaterWaveModelTests.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include "generation/TerrainGenerator.h"
#include "generation/WaterWaveModel.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QVector3D randomDirection(std::mt19937& random) {
    std::uniform_real_distribution<float> value(-1.0f, 1.0f);
    QVector3D result;
    do {
        result = QVector3D(value(random), value(random), value(random));
    } while (result.lengthSquared() < 1e-6f);
    return result.normalized();
}

QVector3D tangentAt(const QVector3D& direction) {
    const QVector3D helper = std::abs(direction.y()) < 0.9f
        ? QVector3D(0.0f, 1.0f, 0.0f)
        : QVector3D(1.0f, 0.0f, 0.0f);
    return QVector3D::crossProduct(helper, direction).normalized();
}

QVector3D rotateAroundAxis(const QVector3D& value, const QVector3D& axis, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return value * cosine
        + QVector3D::crossProduct(axis, value) * sine
        + axis * QVector3D::dotProduct(axis, value) * (1.0f - cosine);
}

float collectiveFromPhases(
    const ResolvedWaterWaveSpec& spec,
    const std::array<float, kWaterWaveComponentCount * 2>& phases) {
    float carrier = 0.0f;
    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const float mainWave = boundedWaterWave(
            phases[static_cast<std::size_t>(i)], spec.requested.shape);
        const float breakupWave = boundedWaterWave(
            phases[static_cast<std::size_t>(i + kWaterWaveComponentCount)],
            spec.requested.shape);
        carrier += spec.requested.components[static_cast<std::size_t>(i)].weight
            * mainWave * (1.0f + spec.requested.crestBreakupStrength * (breakupWave - 1.0f));
    }
    const WaterWaveInteractionShape& interaction = spec.requested.interaction;
    float squaredWeightSum = 0.0f;
    float cubedWeightSum = 0.0f;
    for (const WaterWaveComponent& component : spec.requested.components) {
        squaredWeightSum += component.weight * component.weight;
        cubedWeightSum += component.weight * component.weight * component.weight;
    }
    const float localVariance = interaction.carrierVariance * squaredWeightSum;
    const float localThirdMoment = interaction.carrierThirdMoment * cubedWeightSum;
    return (carrier + interaction.strength * (carrier * carrier - localVariance)
            + interaction.thirdMomentStrength * (carrier * carrier * carrier - localThirdMoment))
        / (1.0f + interaction.strength * (1.0f - localVariance)
            + interaction.thirdMomentStrength * (1.0f - localThirdMoment));
}

ResolvedWaterWaveSpec testSpec(float octaveDetail = 0.8f) {
    ResolvedWaterWaveSpec result;
    result.waterSurfaceRadius = 1.0f;
    result.effectiveGeometryAmplitude = 0.12f;
    result.effectiveGeometryFrequency = 18.0f;
    result.requested.requestedAmplitude = result.effectiveGeometryAmplitude;
    result.requested.requestedFrequency = result.effectiveGeometryFrequency;
    result.requested.speed = 1.7f;
    result.requested.shoreFadeWorldWidth = 0.12f;
    result.requested.shape = resolveBoundedWaterWaveShape(0.7f);
    const WaterSpectrumConfig config = defaultWaterSpectrumConfig();
    const WaterWaveSpectrum spectrum = buildWaterWaveSpectrum(config, octaveDetail);
    result.requested.components = spectrum.components;
    result.requested.activeModeCount = spectrum.activeModeCount;
    result.requested.maximumModeWeight = spectrum.maximumWeight;
    result.requested.requestedMaximumPacketContrast = spectrum.requestedMaximumPacketContrast;
    result.requested.maximumPacketContrast = spectrum.maximumPacketContrast;
    result.requested.maximumCouplingRadians = spectrum.maximumCouplingRadians;
    result.requested.crestBreakupStrength = spectrum.crestBreakupStrength;
    result.requested.primaryRmsGain = spectrum.primaryRmsGain;
    result.requested.groupVelocityRatio = config.groupVelocityRatio;
    result.requested.couplingDriverFrequencyScale = config.couplingDriverFrequencyScale;
    result.requested.driverComponentIndices = spectrum.driverComponentIndices;
    result.requested.bandShares = spectrum.bandShares;
    result.requested.interaction = resolveWaterWaveInteractionShape(
        result.requested.shape, 0.7f, spectrum);
    result.bounds.bottomEpsilon = 0.001f;
    result.bounds.boundEpsilon = 0.0001f;
    result.bounds.outerRadius = 1.0f + result.effectiveGeometryAmplitude + 2.0f * result.bounds.boundEpsilon;
    result.bounds.innerRadius = 1.0f
        - result.requested.interaction.troughRatio * result.effectiveGeometryAmplitude
        - 2.0f * result.bounds.boundEpsilon;
    return result;
}

void testDeterministicSpectrum() {
    const WaterSpectrumConfig config = defaultWaterSpectrumConfig();
    const WaterWaveSpectrum first = buildWaterWaveSpectrum(config, 0.8f);
    const WaterWaveSpectrum second = buildWaterWaveSpectrum(config, 0.8f);
    require(first.activeModeCount == 12, "default water spectrum does not contain twelve active modes");
    require(first.activeModeCount == second.activeModeCount
            && first.maximumWeight == second.maximumWeight
            && first.primaryRmsGain == second.primaryRmsGain,
        "default water spectrum metadata is not deterministic");
    require(first.maximumWeight <= 0.16f + 1e-6f, "a default spectrum mode dominates the field");

    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& a = first.components[static_cast<std::size_t>(i)];
        const WaterWaveComponent& b = second.components[static_cast<std::size_t>(i)];
        require(a.axis == b.axis && a.frequencyMultiplier == b.frequencyMultiplier
                && a.speedMultiplier == b.speedMultiplier && a.phase == b.phase
                && a.weight == b.weight && a.band == b.band
                && a.packetCenter == b.packetCenter && a.packetOrbitAxis == b.packetOrbitAxis
                && a.driverSlots == b.driverSlots && a.breakupAxis == b.breakupAxis
                && a.breakupFrequencyMultiplier == b.breakupFrequencyMultiplier
                && a.breakupSpeedMultiplier == b.breakupSpeedMultiplier
                && a.breakupPhase == b.breakupPhase,
            "two default spectrum generations differ");
        require(std::abs(QVector3D::dotProduct(a.axis, a.breakupAxis)) <= 0.43f,
            "wavelet breakup axis is not sufficiently transverse to its carrier");
        require(std::abs(a.speedMultiplier * a.speedMultiplier - a.frequencyMultiplier) < 2e-6f,
            "deep-water relative dispersion was not applied");
        for (int j = 0; j < i; ++j) {
            const WaterWaveComponent& previous = first.components[static_cast<std::size_t>(j)];
            require(std::abs(a.frequencyMultiplier - previous.frequencyMultiplier) > 1e-5f,
                "water spectrum contains duplicate frequencies");
            const float separation = std::acos(std::clamp(
                std::abs(QVector3D::dotProduct(a.axis, previous.axis)), 0.0f, 1.0f));
            require(separation >= 15.0f * kPi / 180.0f - 1e-5f,
                "water spectrum axes are closer than fifteen antipodal degrees");
        }
    }

    const WaterWaveSpectrum lowDetail = buildWaterWaveSpectrum(config, 0.0f);
    require(lowDetail.activeModeCount == 12, "octave detail zero disabled support modes");
    require(lowDetail.maximumWeight <= 0.16f + 1e-6f,
        "low-detail spectrum contains a dominant mode");
    for (int i = 5; i < kWaterWaveComponentCount; ++i) {
        require(lowDetail.components[static_cast<std::size_t>(i)].frequencyMultiplier <= 1.35f,
            "octave detail zero left a high-frequency octave active");
    }
}

void testAnalyticGradient(const ResolvedWaterWaveSpec& spec, std::mt19937& random) {
    std::uniform_real_distribution<double> time(-10000.0, 10000.0);
    constexpr float epsilon = 2e-4f;
    for (int i = 0; i < 4096; ++i) {
        const QVector3D direction = randomDirection(random);
        const QVector3D tangent = tangentAt(direction);
        const double sampleTime = time(random);
        const WaterWaveFieldSample analytic = sampleWaterWaveFieldWithGradient(spec, direction, sampleTime);
        const QVector3D plusDirection = (direction + epsilon * tangent).normalized();
        const QVector3D minusDirection = (direction - epsilon * tangent).normalized();
        const float finiteDifference = (
            sampleWaterWaveField(spec, plusDirection, sampleTime)
            - sampleWaterWaveField(spec, minusDirection, sampleTime)) / (2.0f * epsilon);
        const float exactDerivative = QVector3D::dotProduct(analytic.angularGradient, tangent);
        const float tolerance = 0.025f + 0.012f * std::abs(exactDerivative);
        require(std::abs(finiteDifference - exactDerivative) <= tolerance,
            "analytic water gradient disagrees with finite difference");
    }
}

void testDynamicPackets(const ResolvedWaterWaveSpec& spec, std::mt19937& random) {
    constexpr double largeTime = 987654321.125;
    const WaterWaveFrameState first = resolveWaterWaveFrameState(spec, largeTime);
    const WaterWaveFrameState second = resolveWaterWaveFrameState(spec, largeTime);
    require(first.temporalPhases == second.temporalPhases
            && first.breakupTemporalPhases == second.breakupTemporalPhases
            && first.microTemporalPhases == second.microTemporalPhases
            && first.packetCenters == second.packetCenters
            && first.activities == second.activities
            && first.coupling0 == second.coupling0
            && first.coupling1 == second.coupling1
            && first.driverTemporalPhases == second.driverTemporalPhases,
        "water frame state is not deterministic for large double time");

    for (int driverSlot = 0; driverSlot < kWaterWaveDriverCount; ++driverSlot) {
        const int driverIndex = spec.requested.driverComponentIndices[static_cast<std::size_t>(driverSlot)];
        require(driverIndex >= 0 && driverIndex < kWaterWaveComponentCount,
            "water driver index is outside the spectrum");
        const WaterWaveComponent& driver = spec.requested.components[static_cast<std::size_t>(driverIndex)];
        require(driver.driverSlots[0] < 0 && driver.driverSlots[1] < 0,
            "water coupling graph contains a modulated driver");
    }

    std::uniform_real_distribution<double> time(-1000000.0, 1000000.0);
    for (int i = 0; i < 20000; ++i) {
        const WaterWaveFieldSample sample = sampleWaterWaveFieldWithGradient(
            spec, randomDirection(random), time(random));
        require(sample.maximumLocalWeight >= 0.0f && sample.maximumLocalWeight <= 0.16001f,
            "dynamic packet weighting produced a dominant mode");
        require(sample.effectiveModeCount >= 1.0f && sample.effectiveModeCount <= 12.001f,
            "effective water mode count is invalid");
        require(sample.fieldVariance >= 0.0f
                && sample.fieldVariance <= spec.requested.interaction.fieldVariance + 2e-6f,
            "local wave variance crossed its conservative bound");
    }
}

float legacySixModeHeight(const QVector3D& direction, const BoundedWaterWaveShape& shape) {
    const std::array<QVector3D, 6> axes = {{
        QVector3D(0.8600f, 0.1000f, 0.5000f).normalized(),
        QVector3D(-0.3414f, 0.2209f, 0.9132f).normalized(),
        QVector3D(0.1208f, 0.9760f, -0.1811f).normalized(),
        QVector3D(-0.7921f, 0.4111f, 0.4512f).normalized(),
        QVector3D(0.5600f, -0.6100f, 0.5600f).normalized(),
        QVector3D(-0.1800f, -0.9300f, -0.3200f).normalized(),
    }};
    const std::array<QVector3D, 6> warpAxes = {{
        QVector3D(0.1700f, -0.8100f, 0.5600f).normalized(),
        QVector3D(0.6700f, 0.5800f, -0.4500f).normalized(),
        QVector3D(-0.7400f, 0.1900f, 0.6400f).normalized(),
        QVector3D(0.3900f, -0.2700f, -0.8800f).normalized(),
        QVector3D(-0.5300f, -0.7200f, 0.4500f).normalized(),
        QVector3D(0.8100f, -0.5100f, -0.2900f).normalized(),
    }};
    constexpr std::array<float, 6> frequency = {{ 1.0f, 1.071f, 1.53f, 1.67f, 2.13f, 2.35f }};
    constexpr std::array<float, 6> warpFrequency = {{ 0.31f, 0.37f, 0.29f, 0.41f, 0.33f, 0.39f }};
    constexpr std::array<float, 6> baseWarp = {{ 0.52f, 0.61f, 0.66f, 0.58f, 0.72f, 0.64f }};
    constexpr std::array<float, 6> weights = {{ 1.0f, 1.0f, 0.336f, 0.288f, 0.1152f, 0.0896f }};
    float result = 0.0f;
    float weightSum = 0.0f;
    for (int i = 0; i < 6; ++i) {
        const float basePhase = 18.0f * frequency[static_cast<std::size_t>(i)]
            * QVector3D::dotProduct(direction, axes[static_cast<std::size_t>(i)])
            + 0.73f * static_cast<float>(i);
        const float warpPhase = 18.0f * warpFrequency[static_cast<std::size_t>(i)]
            * QVector3D::dotProduct(direction, warpAxes[static_cast<std::size_t>(i)])
            + 0.41f + 1.17f * static_cast<float>(i);
        const float phaseValue = basePhase
            + baseWarp[static_cast<std::size_t>(i)] * 0.93f * std::sin(warpPhase);
        result += weights[static_cast<std::size_t>(i)] * boundedWaterWave(phaseValue, shape);
        weightSum += weights[static_cast<std::size_t>(i)];
    }
    return result / weightSum;
}

template <typename SampleFunction>
float maximumSphericalCorrelation(
    const std::vector<QVector3D>& directions,
    const std::vector<QVector3D>& rotationAxes,
    SampleFunction sample) {
    std::vector<float> reference;
    reference.reserve(directions.size());
    double referenceMean = 0.0;
    for (const QVector3D& direction : directions) {
        const float value = sample(direction);
        reference.push_back(value);
        referenceMean += value;
    }
    referenceMean /= static_cast<double>(reference.size());
    double referenceVariance = 0.0;
    for (float value : reference) {
        const double centered = static_cast<double>(value) - referenceMean;
        referenceVariance += centered * centered;
    }

    float maximumCorrelation = 0.0f;
    for (const QVector3D& axis : rotationAxes) {
        for (float angle : { 1.2f, 1.5f, 1.8f, 2.1f, 2.4f }) {
            std::vector<float> shifted;
            shifted.reserve(directions.size());
            double shiftedMean = 0.0;
            for (const QVector3D& direction : directions) {
                const float value = sample(rotateAroundAxis(direction, axis, angle));
                shifted.push_back(value);
                shiftedMean += value;
            }
            shiftedMean /= static_cast<double>(shifted.size());
            double covariance = 0.0;
            double shiftedVariance = 0.0;
            for (std::size_t i = 0; i < shifted.size(); ++i) {
                const double a = static_cast<double>(reference[i]) - referenceMean;
                const double b = static_cast<double>(shifted[i]) - shiftedMean;
                covariance += a * b;
                shiftedVariance += b * b;
            }
            const float correlation = static_cast<float>(std::abs(covariance)
                / std::sqrt(std::max(referenceVariance * shiftedVariance, 1e-20)));
            maximumCorrelation = std::max(maximumCorrelation, correlation);
        }
    }
    return maximumCorrelation;
}

void testSphericalAutocorrelation(const ResolvedWaterWaveSpec& spec) {
    constexpr int sampleCount = 4096;
    constexpr float goldenAngle = kPi * (3.0f - 2.2360679774997896964f);
    std::vector<QVector3D> directions;
    directions.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(sampleCount);
        const float radial = std::sqrt(std::max(1.0f - y * y, 0.0f));
        const float azimuth = goldenAngle * static_cast<float>(i);
        directions.emplace_back(radial * std::cos(azimuth), y, radial * std::sin(azimuth));
    }

    std::vector<QVector3D> newAxes;
    for (const WaterWaveComponent& component : spec.requested.components) {
        if (component.weight > 1e-6f) newAxes.push_back(component.axis);
    }
    const float newPeak = maximumSphericalCorrelation(
        directions, newAxes, [&](const QVector3D& direction) {
            return sampleWaterWaveField(spec, direction, 0.0);
        });

    std::vector<QVector3D> legacyAxes = {
        QVector3D(0.8600f, 0.1000f, 0.5000f).normalized(),
        QVector3D(-0.3414f, 0.2209f, 0.9132f).normalized(),
        QVector3D(0.1208f, 0.9760f, -0.1811f).normalized(),
        QVector3D(-0.7921f, 0.4111f, 0.4512f).normalized(),
        QVector3D(0.5600f, -0.6100f, 0.5600f).normalized(),
        QVector3D(-0.1800f, -0.9300f, -0.3200f).normalized(),
    };
    const float legacyPeak = maximumSphericalCorrelation(
        directions, legacyAxes, [&](const QVector3D& direction) {
            return legacySixModeHeight(direction, spec.requested.shape);
        });
    require(newPeak < 0.35f, "quasiperiodic spectrum has excessive nonzero spherical correlation");
    require(newPeak <= legacyPeak * 0.5f,
        "quasiperiodic spectrum did not halve the legacy six-mode correlation peak");
}

} // namespace

void runWaterWaveModelUnitTests() {
    const WaterParams temperate = waterParamsForPreset(WaterPreset::Temperate);
    const WaterParams storm = waterParamsForPreset(WaterPreset::Storm);
    require(storm.glintIntensity > temperate.glintIntensity, "water presets do not change glint intensity");
    const WaterParams lagoon = waterParamsForPreset(WaterPreset::Lagoon);
    require(temperate.octaveDetail > lagoon.octaveDetail
            && storm.octaveDetail >= temperate.octaveDetail,
        "water presets do not preserve high geometry octave detail");
    require(std::abs(lagoon.shellWaveSpeed - 0.25f) < 1e-6f
            && std::abs(temperate.shellWaveSpeed - 0.50f) < 1e-6f
            && std::abs(storm.shellWaveSpeed - 0.75f) < 1e-6f,
        "water preset speeds do not match the product presets");
    require(std::abs(temperate.shellWaveAmplitude / temperate.shellWaveFrequency - 0.04f) < 1e-6f
            && std::abs(storm.shellWaveAmplitude / storm.shellWaveFrequency - (0.50f / 14.0f)) < 1e-6f
            && std::abs(lagoon.shellWaveAmplitude / lagoon.shellWaveFrequency - (0.30f / 7.0f)) < 1e-6f,
        "density tuning changed the target primary wave height");
    WaterParams editedStorm = storm;
    editedStorm.glintIntensity = 0.37f;
    require(std::abs(resolvedWaterParams(editedStorm).glintIntensity - 0.37f) < 1e-6f,
        "manual water control was overwritten by its preset");
    const WaterSpectrumConfig lagoonSpectrum = waterSpectrumConfigForPreset(WaterPreset::Lagoon);
    const WaterSpectrumConfig temperateSpectrum = waterSpectrumConfigForPreset(WaterPreset::Temperate);
    const WaterSpectrumConfig stormSpectrum = waterSpectrumConfigForPreset(WaterPreset::Storm);
    require(lagoonSpectrum.isotropicFraction < temperateSpectrum.isotropicFraction
            && temperateSpectrum.isotropicFraction < stormSpectrum.isotropicFraction,
        "water presets do not order directional randomness");
    require(lagoonSpectrum.bands[0].phaseCouplingRadians
            < temperateSpectrum.bands[0].phaseCouplingRadians
            && temperateSpectrum.bands[0].phaseCouplingRadians
                < stormSpectrum.bands[0].phaseCouplingRadians,
        "water presets do not order primary phase coupling");
    require(lagoonSpectrum.bands[0].packetOuterAngleDegrees
            > temperateSpectrum.bands[0].packetOuterAngleDegrees
            && temperateSpectrum.bands[0].packetOuterAngleDegrees
                > stormSpectrum.bands[0].packetOuterAngleDegrees,
        "water presets do not order packet localization");

    const SphericalPrimaryWaveScale sphericalScale = deriveSphericalPrimaryWaveScale(0.4f, 10.0f, 1.0f, 162u);
    const float expectedCellArea = 4.0f * kPi / 162.0f;
    require(std::abs(sphericalScale.averageCellArea - expectedCellArea) < 1e-6f,
        "spherical cell area derivation is incorrect");
    require(std::abs(sphericalScale.waveArea * 10.0f - expectedCellArea) < 1e-6f,
        "waves-per-cell did not determine primary wave area");
    require(std::abs(sphericalScale.fullHeight - 0.4f * sphericalScale.waveArea) < 1e-6f,
        "relative height/area did not determine primary wave height");
    require(std::abs(sphericalScale.angularFrequency * sphericalScale.wavelength - 2.0f * kPi) < 2e-5f,
        "primary wave area did not determine its frequency");

    const SphericalPrimaryWaveScale scaledSphere = deriveSphericalPrimaryWaveScale(0.4f, 10.0f, 3.0f, 162u);
    require(std::abs(scaledSphere.fullHeight / 3.0f - sphericalScale.fullHeight) < 2e-6f,
        "relative spherical wave height changed with planet radius");
    require(std::abs(scaledSphere.angularFrequency * 3.0f - sphericalScale.angularFrequency) < 2e-5f,
        "angular wave density changed with planet radius");

    testDeterministicSpectrum();
    std::mt19937 random(0x57A7E2u);
    std::uniform_real_distribution<float> phase(-100000.0f, 100000.0f);
    for (int i = 0; i < 1000000; ++i) {
        const float value = boundedWaterWave(phase(random));
        require(value >= -kWaterWaveBeta - 2e-6f, "bounded wave crossed its trough bound");
        require(value <= 1.0f + 2e-6f, "bounded wave crossed its crest bound");
    }

    constexpr int meanSamples = 262144;
    for (float sharpness : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        const BoundedWaterWaveShape shape = resolveBoundedWaterWaveShape(sharpness);
        double shapedMean = 0.0;
        float minValue = 1.0f;
        float maxValue = -1.0f;
        for (int i = 0; i < meanSamples; ++i) {
            const float value = boundedWaterWave(
                2.0f * kPi * (static_cast<float>(i) + 0.5f) / static_cast<float>(meanSamples), shape);
            shapedMean += value;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        shapedMean /= static_cast<double>(meanSamples);
        require(std::abs(shapedMean) < 3e-6, "sharpened bounded wave gained a DC offset");
        require(minValue >= -shape.troughRatio - 3e-6f, "sharpened wave crossed its trough bound");
        require(maxValue <= 1.0f + 3e-6f, "sharpened wave crossed its crest bound");
    }

    const ResolvedWaterWaveSpec spec = testSpec();
    std::uniform_real_distribution<float> independentPhase(0.0f, 2.0f * kPi);
    std::array<float, kWaterWaveComponentCount * 2> phases{};
    double collectiveMean = 0.0;
    for (int i = 0; i < 1000000; ++i) {
        for (float& value : phases) value = independentPhase(random);
        const float height = collectiveFromPhases(spec, phases);
        collectiveMean += height;
        require(height >= -spec.requested.interaction.troughRatio - 3e-5f && height <= 1.0f + 3e-5f,
            "collective wave crossed its analytic bounds");
    }
    collectiveMean /= 1000000.0;
    require(std::abs(collectiveMean) < 2e-3, "collective interaction gained a DC offset");

    testAnalyticGradient(spec, random);
    testDynamicPackets(spec, random);
    testSphericalAutocorrelation(spec);
    std::uniform_real_distribution<double> time(-1000000.0, 1000000.0);
    std::uniform_real_distribution<float> bedDepth(spec.bounds.bottomEpsilon, 0.45f);
    std::uniform_real_distribution<float> shoreDistance(0.0f, spec.requested.shoreFadeWorldWidth * 3.0f);
    double dynamicMean = 0.0;
    for (int i = 0; i < 100000; ++i) {
        const QVector3D direction = randomDirection(random);
        const float bed = spec.waterSurfaceRadius - bedDepth(random);
        const float shore = shoreDistance(random);
        const double sampleTime = time(random);
        const float height = sampleWaterWaveField(spec, direction, sampleTime);
        dynamicMean += height;
        require(height >= -spec.requested.interaction.troughRatio - 3e-5f && height <= 1.0f + 3e-5f,
            "spherical spectrum crossed its analytic bounds");
        const float radius = sampleWaterSurfaceRadius(
            spec, direction, sampleTime, bed, shore, spec.waterSurfaceRadius);
        require(radius > spec.bounds.innerRadius && radius < spec.bounds.outerRadius,
            "water surface escaped conservative shell");
        require(radius >= bed + spec.bounds.bottomEpsilon - 2e-6f,
            "water trough crossed seabed");
    }
    dynamicMean /= 100000.0;
    require(std::abs(dynamicMean) < 0.025,
        "dynamic packet spectrum gained a visible DC offset");
}
