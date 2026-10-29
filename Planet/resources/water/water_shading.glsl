// Общее затенение воды: одна и та же формула для моря (water_shell.frag) и для
// рек (resources/terrain/terrain.frag). Отличаются только входные данные: у моря
// поверхность берётся из ray-marching, у реки — из ленты русла.

float computeFoamMask(const WaterHit hit, float opticalDepth01) {
    float shoreFoam = 1.0 - hit.surface.shoreMask;
    float thicknessFoam = 1.0 - saturate(hit.visibleThickness * 18.0);
    float waveFoam = saturate(max(hit.surface.waveNormalized, 0.0) * 0.55);
    return saturate((shoreFoam * 0.75 + thicknessFoam * 0.5 + waveFoam * 0.35) * uFoamIntensity);
}

// Ядро шейдинга не знает, откуда взялась поверхность: ему нужны только нормаль,
// цвет основания и «морские» характеристики волны/берега. Благодаря этому река
// получает ровно ту же формулу бликов, отражений, пены и Fresnel, что и море.
vec3 shadeWaterCore(
    vec3 normal,
    vec3 viewDir,
    vec3 lightDir,
    vec3 baseColor,
    float waveNormalized,
    float shoreMask,
    float foamMask,
    out float fresnelOut) {
    float ndv = saturateDot(normal, viewDir);
    float fresnel = saturate((0.02 + 0.98 * pow(1.0 - ndv, 5.0)) * uFresnelStrength);
    fresnelOut = fresnel;

    vec3 reflectDir = reflect(-viewDir, normal);
    vec3 reflection = texture(uEnvMap, reflectDir).rgb * uReflectionStrength;
    reflection = mix(vec3(0.12, 0.18, 0.28), reflection, 0.85);

    float ndl = saturateDot(normal, lightDir);
    vec3 halfDir = safeNormalize(lightDir + viewDir, lightDir);
    float ndh = saturateDot(normal, halfDir);
    float crest = smoothstep(0.18, 0.92, waveNormalized);
    crest = pow(crest, mix(1.8, 0.65, clamp(uCrestSharpness, 0.0, 1.0)));
    float roughness = clamp(mix(uRoughness, uRoughness * 0.42, crest * uCrestSharpness), 0.02, 1.0);
    float specPower = mix(256.0, 18.0, roughness);
    float specular = pow(ndh, specPower) * ndl * uSpecularIntensity;
    float glint = 0.0;
    if (ndh >= uGlintThreshold) {
        glint = pow(saturate((ndh - uGlintThreshold) / max(1.0 - uGlintThreshold, 1e-4)), mix(4.0, 64.0, clamp(uGlintSharpness, 0.0, 1.0))) * uGlintIntensity;
    }
    // A compact solar disk turns the analytic wave normal into discrete flashes.
    float reflectedSunAlignment = saturateDot(reflect(-viewDir, normal), lightDir);
    float reflectedSun = pow(reflectedSunAlignment, 720.0)
        * 210.0 * fresnel * uGlintIntensity * shoreMask;

    vec3 foamColor = mix(vec3(0.85, 0.90, 0.96), vec3(1.0), foamMask);
    float faceContrast = mix(0.78, 1.16, saturate(ndl * 0.72 + ndv * 0.28));
    faceContrast *= mix(0.92, 1.10, crest);
    vec3 litColor = baseColor * (0.22 + 0.78 * ndl) * faceContrast;
    vec3 finalColor = mix(litColor, reflection, fresnel);
    float crestGlint = crest * pow(ndh, mix(42.0, 18.0, uCrestSharpness))
        * uSpecularIntensity * (0.18 + 0.42 * uCrestSharpness);
    finalColor += vec3(
        specular * 0.35 + glint * 0.12 + crestGlint * 0.35
        + reflectedSun);
    finalColor = mix(finalColor, foamColor, foamMask * 0.65);
    return finalColor;
}

vec3 shadeWaterSurface(
    const WaterHit hit,
    vec3 viewDir,
    vec3 lightDir,
    float opticalDepth01,
    float foamMask,
    out float fresnelOut) {
    vec3 baseColor = mix(uShallowColor, uDeepColor, opticalDepth01);
    return shadeWaterCore(
        hit.normal,
        viewDir,
        lightDir,
        baseColor,
        hit.surface.waveNormalized,
        hit.surface.shoreMask,
        foamMask,
        fresnelOut);
}

// Итоговый premultiplied-alpha цвет воды: прозрачность зависит от толщины воды и
// Fresnel так же, как у моря.
vec4 shadeWaterPremultiplied(vec3 color, float fresnel, float alpha) {
    float finalAlpha = clamp(alpha * uOpacity + fresnel * 0.03, 0.0, 1.0);
    return vec4(color * finalAlpha, finalAlpha);
}

vec4 shadeWaterOutput(
    const WaterHit hit,
    float opticalDepth01,
    float foamMask,
    float alpha) {
    vec3 viewDir = safeNormalize(uViewPos - hit.position, vec3(0.0, 0.0, 1.0));
    vec3 lightDir = safeNormalize(-uLightDir, vec3(0.0, 1.0, 0.0));
    float fresnel = 0.0;
    vec3 finalColor = shadeWaterSurface(hit, viewDir, lightDir, opticalDepth01, foamMask, fresnel);
    return shadeWaterPremultiplied(finalColor, fresnel, alpha);
}

// No environment lookup, Fresnel, specular, glints, micro normals or foam.
vec4 shadeFlatWaterOutput(const WaterHit hit, float opticalDepth01, float alpha) {
    vec3 lightDir = safeNormalize(-uLightDir, vec3(0.0, 1.0, 0.0));
    vec3 color = mix(uShallowColor, uDeepColor, opticalDepth01);
    color *= 0.22 + 0.78 * saturateDot(hit.normal, lightDir);
    float finalAlpha = clamp(alpha * uOpacity, 0.0, 1.0);
    return vec4(color * finalAlpha, finalAlpha);
}
