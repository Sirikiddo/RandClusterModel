#version 330 core
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;
in float vOreDensity;
flat in int vOreType;
in vec2 vSurfaceUV;
in float vRockWeight;
flat in uint vRockCell;

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

// ---------- камень на планете ----------
uniform sampler2DArray uRockAlbedoMap;
uniform sampler2DArray uRockNormalMap;
uniform uint uRockAppearanceSeed;
uniform float uRockUVScale;
uniform float uRockNormalStrength;
uniform float uRockStrength;

// ---------- мох на камнях (планета) ----------
uniform sampler2D uMossAlbedoMap;
uniform sampler2D uMossNormalMap;
uniform float uMossAmount;          // общая сила мха 0..1
uniform float uMossScale;           // тайлинг UV текстур мха
uniform float uMossPatchScale;      // частота пятен мха
uniform float uMossCoverage;        // 0..1: какую долю поверхности покрывает мох
uniform float uMossSlope;           // порог "плоскости" камня
uniform float uMossNormalStrength;  // сила нормалей мха

uint rockHash(uint value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float rockRandom(uint channel) {
    return float(rockHash(vRockCell ^ uRockAppearanceSeed ^ (channel * 0x9e3779b9u)) & 0x00ffffffu)
        / 16777216.0;
}

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
    // ---------- камень: текстура + нормал-маппинг ----------
    float material = min(floor(rockRandom(0u) * 3.0), 2.0);
    vec2 rockUV = vSurfaceUV * uRockUVScale * mix(0.85, 1.18, rockRandom(1u));
    if (material == 1.0) rockUV.y *= 1.6;
    rockUV += vec2(rockRandom(2u), rockRandom(3u)) * 11.0;

    vec3 Ngeo = normalize(vNormal);
    vec3 dp1 = dFdx(vWorldPos);
    vec3 dp2 = dFdy(vWorldPos);
    vec2 duv1 = dFdx(rockUV);
    vec2 duv2 = dFdy(rockUV);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    vec3 T;
    vec3 B;
    if (abs(det) < 1e-8) {
        T = normalize(abs(Ngeo.z) < 0.99 ? cross(vec3(0.0, 0.0, 1.0), Ngeo) : vec3(1.0, 0.0, 0.0));
        B = cross(Ngeo, T);
    }
    else {
        float r = 1.0 / det;
        T = normalize((dp1 * duv2.y - dp2 * duv1.y) * r);
        B = normalize((dp2 * duv1.x - dp1 * duv2.x) * r);
    }

    float rockWeight = clamp(vRockWeight * uRockStrength, 0.0, 1.0);

    vec3 rockAlbedo = texture(uRockAlbedoMap, vec3(rockUV, material)).rgb;
    rockAlbedo *= mix(0.94, 1.06, rockRandom(4u));
    vec3 rockNormal = texture(uRockNormalMap, vec3(rockUV, material)).rgb * 2.0 - 1.0;
    rockNormal.xy *= uRockNormalStrength * mix(0.85, 1.12, rockRandom(6u));

    vec3 rockColor = rockAlbedo * 1.6;
    vec3 baseColor = mix(vColor, rockColor, rockWeight);

    vec3 nTan = normalize(mix(vec3(0.0, 0.0, 1.0), normalize(rockNormal), rockWeight));
    vec3 N = normalize(mat3(T, B, Ngeo) * nTan);

    // ---------- МОХ на камнях планеты ----------
    // Принципы (те же, что в песочнице FS_ROCK, но с поправкой на мир):
    //   * отдельное размещение пятен для каждого камня, постоянное до новой генерации;
    //   * upFacing — dot(N_после_нормалей, Ngeo) — мох ложится на «плоские» участки камня;
    //   * цвет берём из шейдера (dark/mid/top), а текстура мха модулирует яркость;
    //   * два масштаба текстуры — мелкие детали поверх крупных;
    //   * рваные края через шум в пороге и затемнение к краю (edge AO).
    if (uMossAmount > 0.001 && rockWeight > 0.5 && rockRandom(5u) < 0.32) {
        vec3 cellDir = normalize(vWorldPos);
        vec3 mossOffset = vec3(rockRandom(7u), rockRandom(8u), rockRandom(9u)) * 23.0;
        vec3 mossPosition = cellDir * uMossPatchScale * 0.35 + vec3(rockUV * 0.85, 0.0) + mossOffset;

        float upFacing = smoothstep(uMossSlope - 0.15, uMossSlope + 0.15,
                                    dot(N, Ngeo));

        // Многослойная patchiness: крупные регионы + средние + мелкие + микро.
        float patchLarge = fbm(mossPosition * 0.35);
        float patchMed   = fbm(mossPosition * 1.1 + vec3(31.0, 17.0, 5.0));
        float patchFine  = fbm(mossPosition * 3.2 + vec3(77.0, 53.0, 11.0));
        float patchMicro = valueNoise(mossPosition * 9.0 + vec3(123.0));

        float patchiness = patchLarge * 0.40
                         + patchMed   * 0.30
                         + patchFine  * 0.20
                         + patchMicro * 0.10;

        // Покрытие задаёт плотность пятен только на выбранной части камней.
        float threshold = mix(0.62, 0.32, clamp(uMossCoverage, 0.0, 1.0));

        // Рваный край: к порогу добавляем мелкий шум, чтобы контур был не «мыльный».
        float edgeWobble = (patchFine  - 0.5) * 0.08
                         + (patchMicro - 0.5) * 0.05;
        float mossShape = smoothstep(threshold - 0.05 + edgeWobble,
                                      threshold + 0.05 + edgeWobble,
                                      patchiness);

        // В самых «мшистых» зонах — плотнее (бугры, подушки).
        float mossDensity = smoothstep(threshold + 0.05, threshold + 0.25, patchiness);

        float mossMask = upFacing * mossShape * uMossAmount * rockWeight * mix(0.65, 0.95, rockRandom(10u));
        if (mossMask > 0.001) {
            // Текстуры мха: два масштаба — крупный рисунок + мелкая деталь.
            vec2 mossUV1 = rockUV * 3.0;
            vec2 mossUV2 = rockUV * 8.0 + vec2(0.5, 0.3);
            vec3 mossTex1 = texture(uMossAlbedoMap, mossUV1).rgb;
            vec3 mossTex2 = texture(uMossAlbedoMap, mossUV2).rgb;
            vec3 mossTex  = mossTex1 * 0.7 + mossTex2 * 0.3;

            // Нормали: тоже два масштаба, нормализуем сумму.
            vec3 mossN1 = texture(uMossNormalMap, mossUV1).rgb * 2.0 - 1.0;
            vec3 mossN2 = texture(uMossNormalMap, mossUV2).rgb * 2.0 - 1.0;
            vec3 mossNormal = normalize(mossN1 + mossN2 * 0.6);
            mossNormal.xy *= uMossNormalStrength * 1.8;

            // Палитра мха — три уровня, чтобы не было «однотонного пятна».
            vec3 mossDeep = vec3(0.04, 0.13, 0.02);   // самая тень (края, впадины)
            vec3 mossMid  = vec3(0.14, 0.36, 0.08);   // база
            vec3 mossTop  = vec3(0.30, 0.62, 0.16);   // освещённые бугры

            // Яркость по текстуре (зелёный канал).
            float texBright = clamp(mossTex.g * 1.9 - 0.15, 0.0, 1.0);
            vec3 mossColor = mix(mossMid, mossTop, texBright);

            // В плотных зонах — светлее и «мясистее».
            mossColor = mix(mossColor, mossTop * 1.1, mossDensity * 0.5);

            // Край — темнее (эффект «укоренения» в камне).
            float edgeAO = smoothstep(0.0, 0.35, mossShape);
            mossColor = mix(mossDeep, mossColor, edgeAO);

            // Финальное смешивание.
            baseColor = mix(baseColor, mossColor, mossMask);
            vec3 nTanMoss = normalize(mix(nTan, mossNormal, mossMask));
            N = normalize(mat3(T, B, Ngeo) * nTanMoss);
        }
    }
    // ---------- /МОХ ----------

    vec3 L = normalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = 0.3 * baseColor;
    vec3 diffuse = 0.7 * diff * baseColor;
    vec3 result  = ambient + diffuse;

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
