const uint SURFACE_INVALID = 0u;
const uint SURFACE_LAND = 1u;
const uint SURFACE_SEA = 3u;

const int WATER_REASON_WATER = 0;
const int WATER_REASON_NO_OUTER_SHELL = 1;
const int WATER_REASON_BOUNDS_VIOLATION = 2;
const int WATER_REASON_UNRESOLVED_INTERVAL = 3;
const int WATER_REASON_LAND_MASK = 4;
const int WATER_REASON_TERRAIN_OCCLUDED = 5;
const int WATER_REASON_INVALID_ATLAS = 6;
const int WATER_REASON_RAYMARCH_MISS_ON_SEA = 7;
const int WATER_REASON_NO_SURFACE_CROSSING = 8;

const float WATER_INF_DISTANCE = 1e20;

struct SphereHit { bool hit; float t0; float t1; };

struct TerrainAtlasSample {
    float radius;
    uint kind;
    float shoreDistance;
};

struct WaterSurfaceSample {
    TerrainAtlasSample terrain;
    float waveRaw;
    float waveNormalized;
    float waveHeight;
    float effectiveAmplitude;
    float shoreMask;
    float radius;
    float fieldVariance;
    float maximumLocalWeight;
    float effectiveModeCount;
    float packetSignal;
    float phaseDisplacement;
    vec3 angularGradient;
};

struct WaterHit {
    bool hit;
    int reason;
    int scanSteps;
    bool thicknessFallback;
    float shellStart;
    float shellEnd;
    float visibleEnd;
    float sceneDistance;
    float tFront;
    float fStart;
    float fEnd;
    vec3 position;
    vec3 dir;
    vec3 normal;
    WaterSurfaceSample surface;
    float visibleThickness;
};

float saturate(float value) { return clamp(value, 0.0, 1.0); }
float saturateDot(vec3 a, vec3 b) { return saturate(dot(a, b)); }

vec3 safeNormalize(vec3 value, vec3 fallbackValue) {
    float lenSq = dot(value, value);
    return lenSq <= 1e-10 ? fallbackValue : value * inversesqrt(lenSq);
}

uint decodeSurfaceKind(float encodedKind) {
    return uint(clamp(int(floor(encodedKind + 0.5)), 0, 3));
}

TerrainAtlasSample sampleTerrainAtlas(vec3 dir) {
    TerrainAtlasSample value;
    value.radius = texture(uPlanetRadiusAtlas, dir).r;
    value.kind = decodeSurfaceKind(texture(uPlanetSurfaceKindAtlas, dir).r);
    value.shoreDistance = texture(uPlanetShoreDistanceAtlas, dir).r;
    return value;
}

vec2 currentScreenUv() { return gl_FragCoord.xy / max(uViewportSize, vec2(1.0)); }

vec3 reconstructWorldPosition(vec2 screenUv, float depth01) {
    vec4 clip = vec4(screenUv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProjection * clip;
    return world.xyz / world.w;
}

float sceneDistanceAtUv(vec2 screenUv) {
    float depth01 = texture(uSceneDepthTex, screenUv).r;
    if (depth01 >= 0.999999) return WATER_INF_DISTANCE;
    return length(reconstructWorldPosition(screenUv, depth01) - uViewPos);
}

SphereHit intersectSphere(vec3 ro, vec3 rd, float radius) {
    SphereHit result;
    result.hit = false;
    result.t0 = result.t1 = 0.0;
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return result;
    float root = sqrt(discriminant);
    result.hit = true;
    result.t0 = -b - root;
    result.t1 = -b + root;
    return result;
}
