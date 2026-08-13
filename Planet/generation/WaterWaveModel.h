#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <QVector3D>

class HexSphereModel;
struct WaterParams;
enum class WaterPreset : int;

inline constexpr int kWaterWaveComponentCount = 12;
inline constexpr int kWaterWaveBandCount = 3;
inline constexpr int kWaterWaveDriverCount = 4;
inline constexpr float kWaterWaveMean = 0.4657596074f;
inline constexpr float kWaterWaveBeta = 0.6184937133f;

struct BoundedWaterWaveShape {
    float exponent = 1.0f;
    float mean = kWaterWaveMean;
    float troughRatio = kWaterWaveBeta;
    float maximumDerivative = 1.0f;
};

struct WaterWaveInteractionShape {
    float strength = 0.0f;
    float thirdMomentStrength = 0.0f;
    float carrierVariance = 0.0f;
    float carrierThirdMoment = 0.0f;
    float fieldVariance = 0.0f;
    float fieldThirdMoment = 0.0f;
    float crestScale = 1.0f;
    float troughRatio = kWaterWaveBeta;
    float maximumDerivative = 1.0f;
};

struct WaterSpectrumBand {
    int modeCount = 0;
    float lowDetailMinFrequencyMultiplier = 1.0f;
    float lowDetailMaxFrequencyMultiplier = 1.0f;
    float minFrequencyMultiplier = 1.0f;
    float maxFrequencyMultiplier = 1.0f;
    float energy = 1.0f;
    float directionSpreadDegrees = 30.0f;
    float phaseCouplingRadians = 0.0f;
    float packetInnerAngleDegrees = 20.0f;
    float packetOuterAngleDegrees = 95.0f;
    float packetContrast = 0.0f;
    float activityFloor = 0.35f;
    float activityCycleSeconds = 43.0f;
    float mixingCycleSeconds = 37.0f;
};

struct WaterSpectrumConfig {
    std::uint32_t seed = 0;
    QVector3D windAxis;
    float isotropicFraction = 0.25f;
    float groupVelocityRatio = 0.5f;
    float couplingDriverFrequencyScale = 0.32f;
    float crestBreakupStrength = 0.90f;
    float crestBreakupFrequencyMultiplier = 1.70f;
    std::array<WaterSpectrumBand, 3> bands{};
};

struct WaterWaveComponent {
    QVector3D axis;
    QVector3D breakupAxis;
    QVector3D packetCenter;
    QVector3D packetOrbitAxis;
    float frequencyMultiplier = 1.0f;
    float speedMultiplier = 1.0f;
    float breakupFrequencyMultiplier = 1.7f;
    float breakupSpeedMultiplier = 1.3f;
    float phase = 0.0f;
    float breakupPhase = 0.0f;
    float weight = 1.0f;
    float phaseCouplingRadians = 0.0f;
    float packetInnerCosine = 1.0f;
    float packetOuterCosine = -1.0f;
    float requestedPacketContrast = 0.0f;
    float packetContrast = 0.0f;
    float activityFloor = 1.0f;
    float activityCycleSeconds = 1.0f;
    float activityPhase = 0.0f;
    float mixingRate = 0.0f;
    float mixingPhase = 0.0f;
    std::array<float, 2> couplingPhaseOffsets{};
    std::array<std::int8_t, 2> driverSlots{{ -1, -1 }};
    std::uint8_t band = 0;
};

struct WaterWaveSpectrum {
    std::array<WaterWaveComponent, kWaterWaveComponentCount> components{};
    std::array<std::uint8_t, kWaterWaveDriverCount> driverComponentIndices{};
    std::array<float, kWaterWaveBandCount> bandShares{};
    int activeModeCount = 0;
    float maximumWeight = 0.0f;
    float primaryRmsGain = 1.0f;
    float requestedMaximumPacketContrast = 0.0f;
    float maximumPacketContrast = 0.0f;
    float maximumCouplingRadians = 0.0f;
    float crestBreakupStrength = 0.0f;
};

struct WaterWaveFrameState {
    std::array<float, kWaterWaveComponentCount> temporalPhases{};
    std::array<float, kWaterWaveComponentCount> breakupTemporalPhases{};
    std::array<std::array<float, 4>, kWaterWaveComponentCount> microTemporalPhases{};
    std::array<QVector3D, kWaterWaveComponentCount> packetCenters{};
    std::array<float, kWaterWaveComponentCount> activities{};
    std::array<float, kWaterWaveComponentCount> coupling0{};
    std::array<float, kWaterWaveComponentCount> coupling1{};
    std::array<float, kWaterWaveDriverCount> driverTemporalPhases{};
};

struct WaterWaveSpec {
    float requestedAmplitude = 0.0f;
    float requestedFrequency = 0.0f;
    float relativeHeightPerArea = 0.0f;
    float primaryWavesPerCell = 0.0f;
    float averageCellArea = 0.0f;
    float primaryWaveArea = 0.0f;
    float primaryWaveHeight = 0.0f;
    float primaryRmsGain = 1.0f;
    int activeModeCount = 0;
    float maximumModeWeight = 0.0f;
    float requestedMaximumPacketContrast = 0.0f;
    float maximumPacketContrast = 0.0f;
    float maximumCouplingRadians = 0.0f;
    float octaveDetail = 0.0f;
    float groupVelocityRatio = 0.5f;
    float couplingDriverFrequencyScale = 0.32f;
    float crestBreakupStrength = 0.90f;
    std::array<std::uint8_t, kWaterWaveDriverCount> driverComponentIndices{};
    std::array<float, kWaterWaveBandCount> bandShares{};
    BoundedWaterWaveShape shape;
    WaterWaveInteractionShape interaction;
    float speed = 0.0f;
    float shoreFadeWorldWidth = 0.0f;
    std::array<WaterWaveComponent, kWaterWaveComponentCount> components{};
};

struct SphericalPrimaryWaveScale {
    float relativeHeightPerArea = 0.0f;
    float wavesPerCell = 0.0f;
    float averageCellArea = 0.0f;
    float waveArea = 0.0f;
    float fullHeight = 0.0f;
    float wavelength = 0.0f;
    float angularFrequency = 0.0f;
};

struct WaterWaveBounds {
    float innerRadius = 0.0f;
    float outerRadius = 0.0f;
    float boundEpsilon = 0.0f;
    float bottomEpsilon = 0.0f;
};

struct ResolvedWaterWaveSpec {
    WaterWaveSpec requested;
    float effectiveGeometryAmplitude = 0.0f;
    float effectiveGeometryFrequency = 0.0f;
    float microFrequency = 0.0f;
    float microNormalStrength = 0.0f;
    float microDrag = 0.0f;
    float crestSharpness = 0.0f;
    float maximumPhaseGradient = 0.0f;
    float maximumWaveAngularGradient = 0.0f;
    float maximumAmplitudeAngularGradient = 0.0f;
    float maximumSeaDepth = 0.0f;
    float waterSurfaceRadius = 0.0f;
    int maximumScanSteps = 64;
    int rootRefinementSteps = 6;
    WaterWaveBounds bounds;
};

enum class WaterHitReason : std::uint8_t {
    Water = 0,
    NoOuterShell,
    BoundsViolation,
    UnresolvedInterval,
    LandMask,
    TerrainOccluded,
    InvalidAtlasSample,
    RaymarchMissOnSea,
    ThicknessFallback,
    NoSurfaceCrossing,
};

struct WaterRayDiagnostic {
    WaterHitReason reason = WaterHitReason::NoOuterShell;
    int scanSteps = 0;
    float fAtStart = 0.0f;
    float fAtEnd = 0.0f;
    float frontDistance = 0.0f;
    float terrainDistance = 0.0f;
};

WaterWaveSpec makeWaterWaveSpec(const WaterParams& params, const HexSphereModel& model);
WaterWaveSpec makeWaterWaveSpec(
    const WaterParams& params,
    const HexSphereModel& model,
    const WaterSpectrumConfig& spectrumConfig);
ResolvedWaterWaveSpec resolveWaterWaveSpec(const WaterWaveSpec& spec, const WaterParams& params, const HexSphereModel& model);
WaterSpectrumConfig defaultWaterSpectrumConfig();
WaterSpectrumConfig waterSpectrumConfigForPreset(WaterPreset preset);
WaterWaveSpectrum buildWaterWaveSpectrum(const WaterSpectrumConfig& config, float octaveDetail);
SphericalPrimaryWaveScale deriveSphericalPrimaryWaveScale(
    float relativeHeightPerArea,
    float wavesPerCell,
    float waterRadius,
    std::size_t cellCount);
BoundedWaterWaveShape resolveBoundedWaterWaveShape(float sharpness);
WaterWaveInteractionShape resolveWaterWaveInteractionShape(
    const BoundedWaterWaveShape& carrier,
    float crestSharpness,
    const WaterWaveSpectrum& spectrum);

float boundedWaterWave(float phase);
float boundedWaterWave(float phase, const BoundedWaterWaveShape& shape);
WaterWaveFrameState resolveWaterWaveFrameState(const ResolvedWaterWaveSpec& spec, double time);
struct WaterWaveFieldSample {
    float height = 0.0f;
    float carrierHeight = 0.0f;
    float fieldVariance = 0.0f;
    float maximumLocalWeight = 0.0f;
    float effectiveModeCount = 0.0f;
    float packetSignal = 0.0f;
    float phaseDisplacement = 0.0f;
    QVector3D angularGradient;
};

float sampleWaterWaveField(const ResolvedWaterWaveSpec& spec, const QVector3D& direction, double time);
WaterWaveFieldSample sampleWaterWaveFieldWithGradient(
    const ResolvedWaterWaveSpec& spec,
    const QVector3D& direction,
    double time);
float depthCappedWaterAmplitude(const ResolvedWaterWaveSpec& spec, float terrainRadius, float signedShoreDistance, float waterRadius);
float sampleWaterSurfaceRadius(
    const ResolvedWaterWaveSpec& spec,
    const QVector3D& direction,
    double time,
    float terrainRadius,
    float signedShoreDistance,
    float waterRadius);
