float computeFoamMask(const WaterHit hit, float opticalDepth01) {
    float shoreFoam = 1.0 - hit.surface.shoreMask;
    float thicknessFoam = 1.0 - saturate(hit.visibleThickness * 18.0);
    float waveFoam = saturate(max(hit.surface.waveNormalized, 0.0) * 0.55);
    return saturate((shoreFoam * 0.75 + thicknessFoam * 0.5 + waveFoam * 0.35) * uFoamIntensity);
}

vec3 shadeWaterSurface(
    const WaterHit hit,
    vec3 viewDir,
    vec3 lightDir,
    float opticalDepth01,
    float foamMask,
    out float fresnelOut) {
    vec3 baseColor = mix(uShallowColor, uDeepColor, opticalDepth01);
    float ndv = saturateDot(hit.normal, viewDir);
    float fresnel = saturate((0.02 + 0.98 * pow(1.0 - ndv, 5.0)) * uFresnelStrength);
    fresnelOut = fresnel;

    vec3 reflectDir = reflect(-viewDir, hit.normal);
    vec3 reflection = texture(uEnvMap, reflectDir).rgb * uReflectionStrength;
    reflection = mix(vec3(0.12, 0.18, 0.28), reflection, 0.85);

    float ndl = saturateDot(hit.normal, lightDir);
    vec3 halfDir = safeNormalize(lightDir + viewDir, lightDir);
    float ndh = saturateDot(hit.normal, halfDir);
    float crest = smoothstep(0.18, 0.92, hit.surface.waveNormalized);
    crest = pow(crest, mix(1.8, 0.65, clamp(uCrestSharpness, 0.0, 1.0)));
    float roughness = clamp(mix(uRoughness, uRoughness * 0.42, crest * uCrestSharpness), 0.02, 1.0);
    float specPower = mix(256.0, 18.0, roughness);
    float specular = pow(ndh, specPower) * ndl * uSpecularIntensity;
    float glint = 0.0;
    if (ndh >= uGlintThreshold) {
        glint = pow(saturate((ndh - uGlintThreshold) / max(1.0 - uGlintThreshold, 1e-4)), mix(4.0, 64.0, clamp(uGlintSharpness, 0.0, 1.0))) * uGlintIntensity;
    }
    // A compact solar disk turns the analytic wave normal into discrete flashes.
    float reflectedSunAlignment = saturateDot(reflect(-viewDir, hit.normal), lightDir);
    float reflectedSun = pow(reflectedSunAlignment, 720.0)
        * 210.0 * fresnel * uGlintIntensity * hit.surface.shoreMask;

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

vec4 shadeWaterOutput(
    const WaterHit hit,
    float opticalDepth01,
    float foamMask,
    float alpha) {
    vec3 viewDir = safeNormalize(uViewPos - hit.position, vec3(0.0, 0.0, 1.0));
    vec3 lightDir = safeNormalize(-uLightDir, vec3(0.0, 1.0, 0.0));
    float fresnel = 0.0;
    vec3 finalColor = shadeWaterSurface(hit, viewDir, lightDir, opticalDepth01, foamMask, fresnel);
    float finalAlpha = clamp(alpha * uOpacity + fresnel * 0.03, 0.0, 1.0);
    return vec4(finalColor * finalAlpha, finalAlpha);
}
