float waterSignedDistance(vec3 ro, vec3 rd, float t, out WaterSurfaceSample surface) {
    vec3 position = ro + rd * t;
    vec3 direction = safeNormalize(position, vec3(0.0, 1.0, 0.0));
    surface = sampleWaterSurfaceHeightOnly(direction);
    return length(position) - surface.radius;
}

bool refineWaterRoot(
    vec3 ro,
    vec3 rd,
    float a,
    float b,
    float fa,
    float fb,
    out float tHit,
    out WaterSurfaceSample surfaceHit) {
    WaterSurfaceSample candidateSurface;
    for (int iteration = 0; iteration < 6; ++iteration) {
        float denominator = fb - fa;
        float candidate = abs(denominator) > 1e-8 ? (a * fb - b * fa) / denominator : 0.5 * (a + b);
        candidate = clamp(candidate, mix(a, b, 0.2), mix(a, b, 0.8));
        float fc = waterSignedDistance(ro, rd, candidate, candidateSurface);
        if (fc <= 0.0) {
            b = candidate;
            fb = fc;
            surfaceHit = candidateSurface;
        } else {
            a = candidate;
            fa = fc;
        }
        if (abs(fc) <= uHitEpsilon) break;
    }
    tHit = b;
    waterSignedDistance(ro, rd, tHit, surfaceHit);
    return true;
}

bool scanFirstWaterRoot(
    vec3 ro,
    vec3 rd,
    float tMin,
    float tMax,
    out float tHit,
    out WaterSurfaceSample surfaceHit,
    out int usedSteps,
    out float fStart,
    out float fEnd,
    out bool unresolved) {
    unresolved = false;
    usedSteps = 0;
    WaterSurfaceSample previousSurface;
    float previousT = tMin;
    float previousF = waterSignedDistance(ro, rd, previousT, previousSurface);
    fStart = previousF;
    fEnd = previousF;
    if (previousF <= 0.0) return false;

    float angularPath = (tMax - tMin) / max(uWaterShellInnerRadius, 1e-5);
    int requestedSteps = int(ceil(uMaximumPhaseGradient * angularPath / (WATER_PI / 8.0))) + 2;
    int scanSteps = clamp(requestedSteps, 4, 64);
    if (requestedSteps > 64) {
        unresolved = true;
        return false;
    }
    float lipschitz = 1.0 + (uEffectiveWaveAmplitude * uMaximumWaveAngularGradient
        + uMaximumAmplitudeGradient)
        / max(uWaterShellInnerRadius, 1e-5);

    for (int step = 1; step <= 64; ++step) {
        if (step > scanSteps) break;
        usedSteps = step;
        float currentT = mix(tMin, tMax, float(step) / float(scanSteps));
        WaterSurfaceSample currentSurface;
        float currentF = waterSignedDistance(ro, rd, currentT, currentSurface);
        fEnd = currentF;

        if (previousF > 0.0 && currentF <= 0.0) {
            unresolved = false;
            return refineWaterRoot(ro, rd, previousT, currentT, previousF, currentF, tHit, surfaceHit);
        }

        float segmentLength = currentT - previousT;
        bool certifiedEmpty = min(previousF, currentF) - lipschitz * segmentLength > 0.0;
        if (!certifiedEmpty && previousF > 0.0 && currentF > 0.0) {
            float subT[5];
            float subF[5];
            subT[0] = previousT;
            subF[0] = previousF;
            subT[4] = currentT;
            subF[4] = currentF;
            for (int sub = 1; sub < 4; ++sub) {
                subT[sub] = mix(previousT, currentT, float(sub) * 0.25);
                WaterSurfaceSample subSurface;
                subF[sub] = waterSignedDistance(ro, rd, subT[sub], subSurface);
            }
            bool subsegmentsCertified = true;
            for (int sub = 0; sub < 4; ++sub) {
                if (subF[sub] > 0.0 && subF[sub + 1] <= 0.0) {
                    unresolved = false;
                    return refineWaterRoot(ro, rd, subT[sub], subT[sub + 1], subF[sub], subF[sub + 1], tHit, surfaceHit);
                }
                float subLength = subT[sub + 1] - subT[sub];
                subsegmentsCertified = subsegmentsCertified
                    && (min(subF[sub], subF[sub + 1]) - lipschitz * subLength > 0.0);
            }
            if (!subsegmentsCertified) unresolved = true;
        }

        previousT = currentT;
        previousF = currentF;
        previousSurface = currentSurface;
    }
    return false;
}

WaterHit traceWater(vec2 screenUv) {
    WaterHit hit;
    hit.hit = false;
    hit.reason = WATER_REASON_NO_OUTER_SHELL;
    hit.scanSteps = 0;
    hit.thicknessFallback = false;
    hit.shellStart = hit.shellEnd = hit.visibleEnd = 0.0;
    hit.sceneDistance = WATER_INF_DISTANCE;
    hit.tFront = hit.fStart = hit.fEnd = 0.0;
    hit.position = vec3(0.0);
    hit.dir = hit.normal = vec3(0.0, 1.0, 0.0);
    hit.visibleThickness = 0.0;

    vec3 ro = uViewPos;
    vec3 rd = safeNormalize(reconstructWorldPosition(screenUv, 1.0) - ro, vec3(0.0, 0.0, -1.0));
    SphereHit outerHit = intersectSphere(ro, rd, uWaterShellOuterRadius);
    if (!outerHit.hit || outerHit.t1 <= 0.0) return hit;

    float tStart = max(outerHit.t0, 0.0);
    float tClosest = max(-dot(ro, rd), tStart);
    SphereHit innerHit = intersectSphere(ro, rd, uWaterShellInnerRadius);
    float tEnd = tClosest;
    if (innerHit.hit && innerHit.t0 > tStart && innerHit.t0 < tClosest) tEnd = innerHit.t0;
    if (tEnd <= tStart) return hit;

    hit.shellStart = tStart;
    hit.shellEnd = tEnd;
    hit.visibleEnd = tEnd;
    hit.position = ro + rd * tStart;
    hit.dir = safeNormalize(hit.position, vec3(0.0, 1.0, 0.0));

    WaterSurfaceSample startSurface;
    hit.fStart = waterSignedDistance(ro, rd, tStart, startSurface);
    hit.surface = startSurface;
    if (hit.fStart <= uBoundEpsilon) {
        hit.reason = WATER_REASON_BOUNDS_VIOLATION;
        return hit;
    }

    WaterSurfaceSample surfaceHit;
    float tFront = 0.0;
    bool unresolved = false;
    if (!scanFirstWaterRoot(
            ro, rd, tStart, tEnd, tFront, surfaceHit,
            hit.scanSteps, hit.fStart, hit.fEnd, unresolved)) {
        if (unresolved) {
            hit.reason = WATER_REASON_UNRESOLVED_INTERVAL;
        } else if (hit.fEnd <= 0.0) {
            hit.reason = WATER_REASON_RAYMARCH_MISS_ON_SEA;
        } else {
            hit.reason = WATER_REASON_NO_SURFACE_CROSSING;
        }
        return hit;
    }

    hit.tFront = tFront;
    hit.position = ro + rd * tFront;
    hit.dir = safeNormalize(hit.position, vec3(0.0, 1.0, 0.0));
    hit.surface = surfaceHit;
    if (surfaceHit.terrain.radius <= 0.0 || surfaceHit.terrain.kind == SURFACE_INVALID) {
        hit.reason = WATER_REASON_INVALID_ATLAS;
        return hit;
    }
    if (surfaceHit.terrain.kind != SURFACE_SEA || surfaceHit.terrain.shoreDistance <= 0.0) {
        hit.reason = WATER_REASON_LAND_MASK;
        return hit;
    }

    // The scan/refinement path evaluates height only. Derivatives and cosine
    // terms are paid once, after the semantic Sea/Land classification.
    hit.surface = sampleWaterSurfaceWithGradient(hit.dir);
    surfaceHit = hit.surface;
    hit.sceneDistance = sceneDistanceAtUv(screenUv);
    if (hit.sceneDistance < tFront - uTerrainContactEpsilon) {
        hit.reason = WATER_REASON_TERRAIN_OCCLUDED;
        return hit;
    }
    if (hit.sceneDistance < WATER_INF_DISTANCE) {
        hit.visibleThickness = max(hit.sceneDistance - tFront, 0.0);
        hit.visibleEnd = hit.sceneDistance;
    } else {
        float radialDepth = max(surfaceHit.radius - surfaceHit.terrain.radius, 0.0);
        hit.visibleThickness = radialDepth / max(abs(dot(rd, hit.dir)), 0.15);
        hit.visibleEnd = tFront + hit.visibleThickness;
        hit.thicknessFallback = true;
    }

    hit.normal = reconstructWaterNormal(hit.dir, surfaceHit, hit.position);
    hit.reason = WATER_REASON_WATER;
    hit.hit = true;
    return hit;
}
