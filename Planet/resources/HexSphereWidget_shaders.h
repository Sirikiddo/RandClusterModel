#pragma once

// РІвЂќР‚РІвЂќР‚РІвЂќР‚ Р РЃР ВµР в„–Р Т‘Р ВµРЎР‚РЎвЂ№ РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚
static const char* VS_WIRE = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos,1.0); }
)GLSL";

static const char* FS_WIRE = R"GLSL(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,0.0,0.0,1.0); }
)GLSL";

static const char* FS_SEL = R"GLSL(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,1.0,0.2,1.0); }
)GLSL";

// РІвЂќР‚РІвЂќР‚РІвЂќР‚ Water shader РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚РІвЂќР‚
static const char* VS_MODEL = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform bool uUseFoliageColor;
uniform float uWindTime;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;
out vec3 vColor;

void main() {
    vec3 animatedPos = aPos;
    if (uUseFoliageColor) {
        // Gentle foliage-only sway in model space.
        float sway = sin(uWindTime * 2.1 + aPos.x * 8.0 + aPos.z * 6.0) * 0.015;
        float gust = sin(uWindTime * 0.9 + aPos.y * 3.5) * 0.010;
        animatedPos.x += sway + gust;
        animatedPos.z += sway * 0.6;
    }

    vec4 worldPos = uModel * vec4(animatedPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    vColor = aColor;
    gl_Position = uMVP * vec4(animatedPos, 1.0);
}
)GLSL";

static const char* FS_MODEL = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
uniform bool uUseTexture;
uniform bool uUseVertexColor;
uniform int uIsCar;

// Uniform'РЎвЂ№ Р Т‘Р В»РЎРЏ Р Т‘Р ВµРЎР‚Р ВµР Р†РЎРЉР ВµР Р† (Р С‘Р В· Р С—Р ВµРЎР‚Р Р†Р С•Р в„– Р Р†Р ВµРЎР‚РЎРѓР С‘Р С‘)
uniform bool uUseFoliageColor;
uniform vec3 uFoliageColor;
uniform vec3 uTrunkColor;

uniform sampler2D uTexture;

void main() {
    vec3 N = normalize(vNormal);
    
    vec3 L;
    float diff;
    
    if (uIsCar == 1) {
        // ========== Р СљР С’Р РЃР ВР СњР С’: РЎРѓР Р†Р ВµРЎвЂљ Р С‘Р Р…Р Р†Р ВµРЎР‚РЎвЂљР С‘РЎР‚Р С•Р Р†Р В°Р Р… ==========
        L = normalize(-uLightDir);  // Р вЂќР В»РЎРЏ Р СР В°РЎв‚¬Р С‘Р Р… uLightDir РЎС“Р В¶Р Вµ Р С—Р С•Р В»Р С•Р В¶Р С‘РЎвЂљР ВµР В»РЎРЉР Р…РЎвЂ№Р в„–, Р С‘РЎРѓР С—Р С•Р В»РЎРЉР В·РЎС“Р ВµР С Р С”Р В°Р С” Р ВµРЎРѓРЎвЂљРЎРЉ
        diff = max(dot(N, L), 0.0);
        
        vec3 baseColor = uColor;
        
        if (uUseTexture) {
            baseColor *= texture(uTexture, vUV).rgb;
        }
        if (uUseVertexColor) {
            baseColor *= vColor;
        }
        
        // Ambient + Diffuse Р Т‘Р В»РЎРЏ Р СР В°РЎв‚¬Р С‘Р Р…РЎвЂ№
        vec3 ambient = 0.3 * baseColor;
        vec3 diffuse = 0.7 * diff * baseColor;
        
        // Specular-Р В±Р В»Р С‘Р С” Р Т‘Р В»РЎРЏ Р СР В°РЎв‚¬Р С‘Р Р…РЎвЂ№
        vec3 V = normalize(uViewPos - vWorldPos);
        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float shininess = 64.0;
        float carSpec = pow(ndh, shininess);
        vec3 specular = 0.45 * carSpec * vec3(1.0, 1.0, 0.9);
        
        FragColor = vec4(ambient + diffuse + specular, 1.0);
        return;
    }
    else {
        // ========== Р вЂќР вЂўР В Р вЂўР вЂ™Р С›: Р В»Р В°Р СР В±Р ВµРЎР‚РЎвЂљ Р С”Р В°Р С” РЎС“ Р С—Р В»Р В°Р Р…Р ВµРЎвЂљРЎвЂ№ ==========
        L = normalize(-uLightDir);
        diff = max(dot(N, L), 0.0);
        
        vec3 baseColor;
        
        if (uUseFoliageColor) {
            // Р С™РЎР‚Р С•Р Р…Р В°
            baseColor = uFoliageColor;
            
            // Р вЂќР С•Р В±Р В°Р Р†Р В»РЎРЏР ВµР С Р Р…Р ВµР В±Р С•Р В»РЎРЉРЎв‚¬РЎС“РЎР‹ Р Р†Р В°РЎР‚Р С‘Р В°РЎвЂ Р С‘РЎР‹ Р Т‘Р В»РЎРЏ Р С•Р В±РЎР‰Р ВµР СР В°
            float leafVar = 0.85 + (sin(vUV.x * 20.0 + vUV.y * 30.0) * 0.15);
            baseColor = baseColor * leafVar;
            
            // Р СџР С•Р Т‘РЎРѓР Р†Р ВµРЎвЂљР С”Р В° Р Р†Р ВµРЎР‚РЎвЂ¦РЎС“РЎв‚¬Р ВµР С”
            if (vUV.y > 0.7) {
                float highlight = (vUV.y - 0.7) * 1.5;
                baseColor += vec3(0.15, 0.1, 0.05) * highlight;
            }
        } else {
            // Р РЋРЎвЂљР Р†Р С•Р В»
            baseColor = uTrunkColor;
            
            // Р СћР ВµР С”РЎРѓРЎвЂљРЎС“РЎР‚Р В° Р С”Р С•РЎР‚РЎвЂ№
            float barkVar = 0.8 + (sin(vUV.x * 50.0) * 0.2);
            baseColor = baseColor * barkVar;
        }
        
        // Р С›Р В Р ВР вЂњР ВР СњР С’Р вЂєР В¬Р СњР С›Р вЂў Р С›Р РЋР вЂ™Р вЂўР В©Р вЂўР СњР ВР вЂў Р С‘Р В· Р С—Р ВµРЎР‚Р Р†Р С•Р в„– Р Р†Р ВµРЎР‚РЎРѓР С‘Р С‘
        vec3 ambient = 0.3 * baseColor;
        vec3 diffuse = 0.7 * diff * baseColor;
        vec3 result = ambient + diffuse;
        
        FragColor = vec4(result, 1.0);
        return;
    }
}
)GLSL";

static const char* VS_FACTORY = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;
out vec3 vColor;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_FACTORY = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;
in vec3 vColor;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
uniform bool uUseTexture;
uniform bool uUseVertexColor;
uniform sampler2D uTexture;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);

    vec3 baseColor = uColor;
    if (uUseTexture) {
        baseColor *= texture(uTexture, vUV).rgb;
    }
    if (uUseVertexColor) {
        baseColor *= vColor;
    }

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 48.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5);

    vec3 ambient = baseColor * 0.38;
    vec3 diffuse = baseColor * diff * 0.72;
    vec3 specular = vec3(0.85, 0.9, 1.0) * spec * 0.22;
    vec3 industrialTint = vec3(0.06, 0.08, 0.1) * rim;

    vec3 finalColor = ambient + diffuse + specular + industrialTint;
    FragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
)GLSL";

static const char* VS_STEAM = R"GLSL(
#version 330 core
layout(location=0) in vec3 aEmitter;
layout(location=1) in float aSeed;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uTime;
uniform vec3 uViewPos;

out float vAlpha;
out float vSoftness;

void main() {
    float cycle = fract(uTime * 0.42 + aSeed);
    float rise = cycle * 4.1;
    float swirl = uTime * 1.35 + aSeed * 19.0;
    float lateralDrift = sin(uTime * 0.9 + aSeed * 31.0) * 0.08;

    vec3 localPos = aEmitter + vec3(
        sin(swirl) * (0.06 + cycle * 0.14) + lateralDrift,
        rise,
        cos(swirl * 0.8) * (0.05 + cycle * 0.12)
    );

    vec4 worldPos = uModel * vec4(localPos, 1.0);
    gl_Position = uMVP * vec4(localPos, 1.0);

    float distToCamera = max(length(uViewPos - worldPos.xyz), 0.001);
    gl_PointSize = mix(10.0, 22.0, cycle) / (0.16 * distToCamera + 0.55);

    vAlpha = smoothstep(0.0, 0.10, cycle) * (1.0 - smoothstep(0.45, 1.0, cycle));
    vSoftness = mix(1.2, 2.4, cycle);
}
)GLSL";

static const char* FS_STEAM = R"GLSL(
#version 330 core
in float vAlpha;
in float vSoftness;

out vec4 FragColor;

void main() {
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float d = dot(uv, uv);
    if (d > 1.0) {
        discard;
    }

    float soft = pow(max(1.0 - d, 0.0), vSoftness);
    vec3 steamColor = mix(vec3(0.42, 0.43, 0.45), vec3(0.62, 0.63, 0.65), 0.45);
    float dissolve = smoothstep(0.02, 0.45, soft) * (1.0 - smoothstep(0.55, 0.98, 1.0 - vAlpha));
    FragColor = vec4(steamColor, soft * vAlpha * dissolve * 0.62);
}
)GLSL";


// Осевая линия реки: постоянная толщина в пикселях, чтобы русло не сжималось
// в ноль на ракурсах, где лента почти параллельна лучу зрения.
static const char* VS_RIVER_CORE = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
out vec3 vWorld;
void main() {
    vWorld = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* GS_RIVER_CORE = R"GLSL(
#version 330 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
uniform vec2 uViewportSize;
uniform float uWidthPx;
in vec3 vWorld[];
out vec3 vWorldOut;
void main() {
    vec4 c0 = gl_in[0].gl_Position;
    vec4 c1 = gl_in[1].gl_Position;
    if (c0.w <= 1e-5 || c1.w <= 1e-5) {
        return;
    }
    vec2 s0 = c0.xy / c0.w;
    vec2 s1 = c1.xy / c1.w;
    vec2 dir = (s1 - s0) * uViewportSize;
    if (length(dir) < 1e-6) {
        dir = vec2(0.0, 1.0);
    }
    dir = normalize(dir);
    vec2 normal = vec2(-dir.y, dir.x);
    vec2 offset = normal * (uWidthPx * 2.0) / max(uViewportSize, vec2(1.0));

    vec4 a0 = c0; a0.xy += offset * c0.w;
    vec4 b0 = c0; b0.xy -= offset * c0.w;
    vec4 a1 = c1; a1.xy += offset * c1.w;
    vec4 b1 = c1; b1.xy -= offset * c1.w;

    vWorldOut = vWorld[0]; gl_Position = a0; EmitVertex();
    vWorldOut = vWorld[0]; gl_Position = b0; EmitVertex();
    vWorldOut = vWorld[1]; gl_Position = a1; EmitVertex();
    vWorldOut = vWorld[1]; gl_Position = b1; EmitVertex();
    EndPrimitive();
}
)GLSL";

static const char* FS_RIVER_CORE = R"GLSL(
#version 330 core
in vec3 vWorldOut;
uniform vec3 uViewPos;
out vec4 FragColor;
void main() {
    // Не рисуем линию на обратной стороне планеты.
    if (dot(normalize(vWorldOut), normalize(uViewPos)) < 0.0) {
        discard;
    }
    FragColor = vec4(0.02, 0.07, 0.12, 1.0);
}
)GLSL";

// Rock sandbox shader
static const char* VS_ROCK = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_ROCK = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform vec3  uLightDir;        // ОТ поверхности К свету (нормированный)
uniform vec3  uViewPos;
uniform vec3  uAlbedo;          // базовый цвет камня
uniform float uUVTiling;        // сколько раз размножить UV (тайлинг)
uniform float uNoiseScale;      // частота шума: меньше = крупнее камень
uniform float uNormalStrength;  // сила рельефа (0 = плоско)
uniform float uParallaxScale;   // 0 = без параллакса, 0.02..0.05 = лёгкое выдавливание
uniform float uRoughness;       // 0..1: 0 гладкий камень, 1 матовый грубый
uniform float uNormalFlipY;     // 1 или -1: переворот зелёного канала (для зеркальных UV)
uniform int   uDebugMode;       // 0 финал, 1 нормаль, 2 N.L, 3 UV, 4 оси TBN, 5 высота, 6 альбедо
uniform int       uUseBaked;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uRoughMap;
uniform sampler2D uHeightMap;
uniform float     uBakeNormalStrength;  // сила, с которой пекалась карта нормалей:
                                        // uNormalStrength работает как множитель к ней

// ---- маска второго материала (мох) ----
uniform sampler2D uMossAlbedoMap;
uniform sampler2D uMossNormalMap;
uniform float uMossRoughness;   // шероховатость второго материала
uniform float uMaskScale;       // частота пятен маски
uniform float uMaskAmount;      // 0 = маски нет, 1 = полная сила
uniform float uMaskSlope;       // порог "смотрит наружу": ближе к 1 — зарастают только ровные бугры

// ---------------------------------------------------------------- шум
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Грубый камень: обычный fbm (общая форма) + "хребты" 1-|2n-1| (грани, сколы, трещины).
float rockHeight(vec2 uv) {
    float sum = 0.0;
    float ridge = 0.0;
    float amp = 0.5;
    vec2 p = uv;

    for (int i = 0; i < 5; ++i) {
        float n = valueNoise(p);
        sum += amp * n;
        ridge += amp * (1.0 - abs(n * 2.0 - 1.0));
        p = p * 2.03 + vec2(17.1, 9.2);
        amp *= 0.5;
    }

    return sum * 0.65 + ridge * 0.35;
}

void main() {
    // ---------------------------------------------------- TBN из производных
    vec2 uv0 = vUV * uUVTiling;

    vec3 N = normalize(vNormal);
    vec3 dp1 = dFdx(vWorldPos);
    vec3 dp2 = dFdy(vWorldPos);
    vec2 duv1 = dFdx(uv0);
    vec2 duv2 = dFdy(uv0);

    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    vec3 T;
    vec3 B;

    if (abs(det) < 1e-8) {
        // UV вырождены — берём любую ортонормальную пару к нормали.
        T = normalize(abs(N.z) < 0.99 ? cross(vec3(0.0, 0.0, 1.0), N) : vec3(1.0, 0.0, 0.0));
        B = normalize(cross(N, T));
    }
    else {
        float r = 1.0 / det;
        T = normalize((dp1 * duv2.y - dp2 * duv1.y) * r);
        B = normalize((dp2 * duv1.x - dp1 * duv2.x) * r);
    }

        // ------------------------------------------- одношаговый параллакс (опция)
    // Работает одинаково в обоих режимах: высота берётся из карты или из шума.
    vec3 Vw = normalize(uViewPos - vWorldPos);
    vec2 uv = uv0;

    if (uParallaxScale > 0.0) {
        vec3 Vt = vec3(dot(Vw, T), dot(Vw, B), dot(Vw, N));
        float h0 = (uUseBaked == 1)
            ? texture(uHeightMap, uv0).r
            : rockHeight(uv0 * uNoiseScale);
        uv = uv0 - (Vt.xy / max(Vt.z, 0.35)) * (h0 * uParallaxScale);
    }

    // ------------- рельеф, альбедо и шероховатость: шум или текстуры -------------
    float hC;
    vec3 albedo;
    vec3 nTangent;
    float rough;

    if (uUseBaked == 1) {
        hC = texture(uHeightMap, uv).r;
        albedo = texture(uAlbedoMap, uv).rgb;
        rough = clamp(texture(uRoughMap, uv).r * (uRoughness / 0.85), 0.0, 1.0);

        vec3 nTex = texture(uNormalMap, uv).rgb * 2.0 - 1.0;
        nTex.y *= uNormalFlipY;
        nTex.xy *= (uNormalStrength / max(uBakeNormalStrength, 1e-4));
        nTangent = normalize(nTex);
    }
    else {
        const float eps = 1.0 / 256.0;
        hC = rockHeight(uv * uNoiseScale);
        float hX = rockHeight((uv + vec2(eps, 0.0)) * uNoiseScale);
        float hY = rockHeight((uv + vec2(0.0, eps)) * uNoiseScale);

        // Наклон поверхности из карты высот -> нормаль в касательном пространстве.
        vec2 slope = vec2(hX - hC, hY - hC) * (uNormalStrength / eps);
        nTangent = normalize(vec3(-slope.x, -slope.y * uNormalFlipY, 1.0));

        // Трещины темнее: высота подмешивается в альбедо.
        albedo = uAlbedo * mix(0.55, 1.15, hC);
        rough = uRoughness;
    }

    vec3 Nw = normalize(mat3(T, B, N) * nTangent);

    // ================= МАСКА ВТОРОГО МАТЕРИАЛА (мох) =================
    float upFacing = smoothstep(uMaskSlope - 0.06, uMaskSlope + 0.06, dot(Nw, N));
    float patchNoise = valueNoise(uv * uMaskScale) * 0.65
                     + valueNoise(uv * uMaskScale * 2.7) * 0.35;
    float spots = smoothstep(0.42, 0.68, patchNoise);
    float mossMask = upFacing * spots * clamp(uMaskAmount, 0.0, 1.0);

    vec3 mossAlbedo = texture(uMossAlbedoMap, uv).rgb;
    vec3 mossTexel = texture(uMossNormalMap, uv).rgb * 2.0 - 1.0;
    mossTexel.y *= uNormalFlipY;
    mossTexel.xy *= (uNormalStrength / max(uBakeNormalStrength, 1e-4));
    vec3 mossTangent = normalize(mossTexel);

    albedo = mix(albedo, mossAlbedo, mossMask);
    nTangent = normalize(mix(nTangent, mossTangent, mossMask));
    rough = mix(rough, uMossRoughness, mossMask);

    Nw = normalize(mat3(T, B, N) * nTangent);

    // ------------------------------------------------------------- свет
    vec3 L = normalize(uLightDir);
    vec3 H = normalize(L + Vw);

    float ndl = max(dot(Nw, L), 0.0);

    float roughClamped = clamp(rough, 0.0, 1.0);
    float specPower = mix(96.0, 6.0, roughClamped);
    float specStrength = mix(0.35, 0.02, roughClamped) * (1.0 - roughClamped * 0.5);
    float spec = pow(max(dot(Nw, H), 0.0), specPower) * specStrength;

    // Небо как дешёвый ambient: сверху светлее, снизу темнее.
    float sky = Nw.y * 0.5 + 0.5;
    vec3 ambient = albedo * mix(0.10, 0.22, sky);
    vec3 diffuse = albedo * ndl;

    vec3 color = ambient + diffuse + vec3(spec);

    // -------------------------------------------------------- отладка
    if (uDebugMode == 1) { FragColor = vec4(Nw * 0.5 + 0.5, 1.0); return; }
    if (uDebugMode == 2) { FragColor = vec4(vec3(ndl), 1.0); return; }
    if (uDebugMode == 3) {
        vec2 g = floor(vUV * 8.0);
        float cell = mod(g.x + g.y, 2.0);
        FragColor = vec4(vec3(cell), 1.0);
        return;
    }
    if (uDebugMode == 4) {
        // Для плоской плитки, смотрящей в +Z, должно быть белым (1,1,1):
        // T вдоль +X, B вдоль +Y, N вдоль +Z. Голубой = T и B поменялись местами,
        // жёлтый/пурпурный = перевёрнута одна из осей, чёрный = TBN вырожден.
        FragColor = vec4(clamp(vec3(dot(T, vec3(1.0, 0.0, 0.0)),
                                    dot(B, vec3(0.0, 1.0, 0.0)),
                                    dot(N, vec3(0.0, 0.0, 1.0))), 0.0, 1.0), 1.0);
        return;
    }
    if (uDebugMode == 5) { FragColor = vec4(vec3(hC), 1.0); return; }
    if (uDebugMode == 6) { FragColor = vec4(albedo, 1.0); return; }

    // 7..10 — посмотреть на сами запёкшиеся карты
    if (uDebugMode == 7) { FragColor = vec4(texture(uAlbedoMap, uv).rgb, 1.0); return; }
    if (uDebugMode == 8) { FragColor = vec4(texture(uNormalMap, uv).rgb, 1.0); return; }
    if (uDebugMode == 9) { FragColor = vec4(vec3(texture(uRoughMap, uv).r), 1.0); return; }
    if (uDebugMode == 10) { FragColor = vec4(vec3(texture(uHeightMap, uv).r), 1.0); return; }

// 11..13 — разбор маски по частям
    if (uDebugMode == 11) { FragColor = vec4(vec3(mossMask), 1.0); return; }
    if (uDebugMode == 12) { FragColor = vec4(vec3(upFacing), 1.0); return; }
    if (uDebugMode == 13) { FragColor = vec4(vec3(spots), 1.0); return; }

    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)GLSL";
