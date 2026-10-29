#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aNormal;
layout(location=3) in vec2 aOreData;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform bool uIsRiver;

// Волны реки берутся из той же модели, что и волны моря
// (resources/water/water_wave_field.glsl), поэтому у устья поверхность русла
// совпадает с поверхностью моря, а не уходит под неё. Геометрическая амплитуда
// в русле намеренно меньше морской: у реки нет разгона волны, и вспухать на
// полклетки она не должна. Наклон для затенения при этом остаётся морским —
// см. uRiverWaveNormalAmplitude в terrain.frag.
uniform float uRiverWaveAmplitude;

#include "../water/water_math.glsl"
#include "../water/water_wave_field.glsl"

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorldPos;
out float vOreDensity;
flat out int vOreType;

void main() {
    vec3 position = aPos;
    if (uIsRiver) {
        // vColor.x у вершин русла — близость к берегу (0 центр, 1 кромка).
        float bank = clamp(aColor.x, 0.0, 1.0);
        vec3 dir = normalize(aPos);
        float carrierHeight = 0.0;
        float fieldVariance = 0.0;
        float maximumLocalWeight = 0.0;
        float effectiveModeCount = 0.0;
        float packetSignal = 0.0;
        float phaseDisplacement = 0.0;
        float waveHeight = sampleWaveHeight(
            dir,
            carrierHeight,
            fieldVariance,
            maximumLocalWeight,
            effectiveModeCount,
            packetSignal,
            phaseDisplacement);
        // У кромки волна гаснет: берег остаётся приклеенным к рельефу.
        float openWater = 1.0 - smoothstep(0.30, 0.95, bank);
        position = dir * (length(aPos) + uRiverWaveAmplitude * waveHeight * openWater);
    }

    vec4 worldPos = uModel * vec4(position, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vColor = aColor;
    vOreDensity = aOreData.x;
    vOreType = int(aOreData.y + 0.5);
    gl_Position = uMVP * vec4(position, 1.0);
}
