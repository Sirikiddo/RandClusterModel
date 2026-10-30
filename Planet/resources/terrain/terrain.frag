#version 330 core
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;
in float vOreDensity;
flat in int vOreType;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform bool uOreVisualizationEnabled;
uniform vec3 uRoadColor;
uniform bool uIsRoad;

// ===== Реки =====
// Русло рисуется тем же проходом, что и рельеф, но затеняется общей с морем
// функцией (resources/water/water_shading.glsl) и по общей волновой модели
// (resources/water/water_wave_field.glsl) — река выглядит как море.
uniform bool uIsRiver;
uniform float uRiverWaveAmplitude;
uniform float uRiverWaveNormalAmplitude;
uniform float uRiverWaterDepth;
// Допуск сравнения с глубиной террейна. Река лежит на рельефе, поэтому на
// кромках клиффов и скосах отдельные пиксели могут оказаться чуть ближе;
// допуск не даёт им вырезать русло. Настоящие горы впереди ближе на большее
// расстояние и по-прежнему перекрывают реку.
uniform float uRiverDepthTolerance;
uniform float uDepthOpticalDensity;
uniform float uDepthAlphaDensity;

// Вода моря (те же значения, что получает WaterRenderer).
uniform samplerCube uEnvMap;
uniform vec3 uShallowColor;
uniform vec3 uDeepColor;
uniform float uFresnelStrength;
uniform float uSpecularIntensity;
uniform float uGlintIntensity;
uniform float uGlintThreshold;
uniform float uGlintSharpness;
uniform float uRoughness;
uniform float uReflectionStrength;
uniform float uOpacity;
uniform float uCrestSharpness;
uniform float uFoamIntensity;

// Атласы и параметры ray-marching нужны только функциям water_common.glsl;
// в проходе рельефа они не используются и остаются неактивными.
uniform samplerCube uPlanetRadiusAtlas;
uniform samplerCube uPlanetSurfaceKindAtlas;
uniform samplerCube uPlanetShoreDistanceAtlas;
uniform sampler2D uSceneDepthTex;
uniform mat4 uInvViewProjection;

// Порядок важен: water_common.glsl пользуется saturate/safeNormalize из
// water_math.glsl и view-данными из water_wave_field.glsl.
#include "../water/water_math.glsl"
#include "../water/water_wave_field.glsl"
#include "../water/water_common.glsl"
#include "../water/water_shading.glsl"

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash31(i), hash31(i + vec3(1,0,0)), f.x),
            mix(hash31(i + vec3(0,1,0)), hash31(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash31(i + vec3(0,0,1)), hash31(i + vec3(1,0,1)), f.x),
            mix(hash31(i + vec3(0,1,1)), hash31(i + vec3(1,1,1)), f.x), f.y),
        f.z);
}

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        value += amplitude * valueNoise(p);
        p = p * 2.03 + vec3(17.1, 9.2, 13.7);
        amplitude *= 0.5;
    }
    return value;
}

vec3 oreColor(int oreType) {
    if (oreType == 1) return vec3(0.72, 0.30, 0.10);
    if (oreType == 2) return vec3(0.95, 0.43, 0.12);
    if (oreType == 3) return vec3(1.00, 0.82, 0.12);
    if (oreType == 4) return vec3(0.22, 0.72, 1.00);
    return vec3(0.0);
}

void main() {
    if (uIsRiver) {
        if (uRiverDepthTolerance >= 0.0) {
            float terrainDistance = sceneDistanceAtUv(currentScreenUv());
            float riverDistance = length(vWorldPos - uViewPos);
            if (terrainDistance + uRiverDepthTolerance < riverDistance) {
                discard;
            }
        }
        // vColor.x — близость к берегу (0 центр русла, 1 кромка).
        float bank = clamp(vColor.x, 0.0, 1.0);
        vec3 dir = normalize(vWorldPos);
        float radius = max(length(vWorldPos), 1e-5);

        // У кромки волна гаснет (как у моря на мелководье): берег остаётся
        // приклеенным к рельефу, а в середине русла волны полноценные.
        float openWater = 1.0 - smoothstep(0.30, 0.95, bank);
        // Наклон поверхности — морской (рябь и блики как у моря), а подъём
        // геометрии в vertex shader слабее: русло не должно вспухать волнами.
        float amplitude = uRiverWaveNormalAmplitude * openWater;

        float waveHeight = 0.0;
        float carrierHeight = 0.0;
        float fieldVariance = 0.0;
        float maximumLocalWeight = 0.0;
        float effectiveModeCount = 0.0;
        float packetSignal = 0.0;
        float phaseDisplacement = 0.0;
        vec3 waveGradient = vec3(0.0);
        sampleWaveHeightGradient(
            dir,
            waveHeight,
            carrierHeight,
            fieldVariance,
            maximumLocalWeight,
            effectiveModeCount,
            packetSignal,
            phaseDisplacement,
            waveGradient);

        vec3 gradient = amplitude * waveGradient;
        gradient += amplitude * uMicroNormalStrength
            * sampleMicroWaveAngularGradient(dir, vWorldPos);

        // Та же нормаль, что у моря: наклон поверхности = градиент волны / радиус.
        vec3 macroNormal = safeNormalize(dir - gradient / radius, dir);
        vec3 normal = safeNormalize(mix(dir, macroNormal, openWater), dir);

        // Толщина воды в русле: максимальна на оси и нулевая у кромок, поэтому
        // цвет и прозрачность совпадают с морем на такой же глубине.
        float thickness = uRiverWaterDepth * (1.0 - bank * bank);
        float opticalDepth01 = 1.0 - exp(-thickness * uDepthOpticalDensity);
        float alpha = 1.0 - exp(-thickness * uDepthAlphaDensity);

        // Пена: у кромок русла и на гребнях волн — как у моря у берега.
        float foam = smoothstep(0.78, 1.0, bank) * 0.9;
        foam += smoothstep(0.55, 1.0, waveHeight) * openWater * 0.35;
        float foamMask = clamp(foam * uFoamIntensity, 0.0, 1.0);

        vec3 viewDir = normalize(uViewPos - vWorldPos);
        vec3 lightDir = normalize(-uLightDir);
        vec3 baseColor = mix(uShallowColor, uDeepColor, opticalDepth01);
        float fresnel = 0.0;
        vec3 color = shadeWaterCore(
            normal,
            viewDir,
            lightDir,
            baseColor,
            waveHeight,
            openWater,
            foamMask,
            fresnel);
        // Река не должна просвечивать на тонких/наклонных участках: иначе
        // кажется, что русло разрывается. Держим минимальную непрозрачность.
        alpha = max(alpha, 0.85);
        FragColor = shadeWaterPremultiplied(color, fresnel, alpha);
        return;
    }
    if (uIsRoad) {
        vec3 N = normalize(vNormal);
        vec3 L = normalize(-uLightDir);
        float diff = max(dot(N, L), 0.0);
        vec3 ambient = 0.3 * uRoadColor;
        vec3 diffuse = 0.7 * diff * uRoadColor;
        FragColor = vec4(ambient + diffuse, 1.0);
        return;
    }
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = 0.3 * vColor;
    vec3 diffuse = 0.7 * diff * vColor;
    vec3 result = ambient + diffuse;
    if (uOreVisualizationEnabled && vOreDensity > 0.0 && vOreType > 0) {
        vec3 p = normalize(vWorldPos) * 34.0;
        float ridge = 1.0 - abs(fbm(p) * 2.0 - 1.0);
        float detail = fbm(p * 2.7 + float(vOreType) * 11.3);
        float vein = smoothstep(0.72 - 0.20 * vOreDensity,
                                0.86 - 0.10 * vOreDensity, ridge);
        float mineral = vein * mix(0.65, 1.0, smoothstep(0.48, 0.78, detail)) * vOreDensity;
        float halo = smoothstep(0.58 - 0.12 * vOreDensity,
                                0.76 - 0.08 * vOreDensity, ridge) * vOreDensity;
        vec3 mineralColor = oreColor(vOreType);
        result = mix(result, mineralColor, clamp(mineral * 0.88, 0.0, 0.88));
        result += mineralColor * halo * 0.16;
    }
    FragColor = vec4(result, 1.0);
}
