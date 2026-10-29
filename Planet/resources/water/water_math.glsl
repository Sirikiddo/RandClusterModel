// Базовые математические хелперы воды. Подключается первым: остальные файлы
// воды (water_common.glsl, water_wave_field.glsl) на них опираются.

float saturate(float value) { return clamp(value, 0.0, 1.0); }
float saturateDot(vec3 a, vec3 b) { return saturate(dot(a, b)); }

vec3 safeNormalize(vec3 value, vec3 fallbackValue) {
    float lenSq = dot(value, value);
    return lenSq <= 1e-10 ? fallbackValue : value * inversesqrt(lenSq);
}
