#pragma once

// в”Ђв”Ђв”Ђ РЁРµР№РґРµСЂС‹ в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
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

static const char* VS_TERRAIN = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aNormal;
layout(location=3) in vec2 aOreData;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix; 
uniform vec3 uRoadColor;    

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorldPos;
out float vOreDensity;
flat out int vOreType;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vColor = aColor;
    vOreDensity = aOreData.x;
    vOreType = int(aOreData.y + 0.5);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_TERRAIN = R"GLSL(
#version 330 core
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;
in float vOreDensity;
flat in int vOreType;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform bool uOreVisualizationEnabled;
uniform vec3 uRoadColor;
uniform bool uIsRoad;

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
)GLSL";

static const char* FS_SEL = R"GLSL(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,1.0,0.2,1.0); }
)GLSL";

// в”Ђв”Ђв”Ђ Water shader в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
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

// Uniform'С‹ РґР»СЏ РґРµСЂРµРІСЊРµРІ (РёР· РїРµСЂРІРѕР№ РІРµСЂСЃРёРё)
uniform bool uUseFoliageColor;
uniform vec3 uFoliageColor;
uniform vec3 uTrunkColor;

uniform sampler2D uTexture;

void main() {
    vec3 N = normalize(vNormal);
    
    vec3 L;
    float diff;
    
    if (uIsCar == 1) {
        // ========== РњРђРЁРРќРђ: СЃРІРµС‚ РёРЅРІРµСЂС‚РёСЂРѕРІР°РЅ ==========
        L = normalize(-uLightDir);  // Р”Р»СЏ РјР°С€РёРЅ uLightDir СѓР¶Рµ РїРѕР»РѕР¶РёС‚РµР»СЊРЅС‹Р№, РёСЃРїРѕР»СЊР·СѓРµРј РєР°Рє РµСЃС‚СЊ
        diff = max(dot(N, L), 0.0);
        
        vec3 baseColor = uColor;
        
        if (uUseTexture) {
            baseColor *= texture(uTexture, vUV).rgb;
        }
        if (uUseVertexColor) {
            baseColor *= vColor;
        }
        
        // Ambient + Diffuse РґР»СЏ РјР°С€РёРЅС‹
        vec3 ambient = 0.3 * baseColor;
        vec3 diffuse = 0.7 * diff * baseColor;
        
        // Specular-Р±Р»РёРє РґР»СЏ РјР°С€РёРЅС‹
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
        // ========== Р”Р•Р Р•Р’Рћ: Р»Р°РјР±РµСЂС‚ РєР°Рє Сѓ РїР»Р°РЅРµС‚С‹ ==========
        L = normalize(-uLightDir);
        diff = max(dot(N, L), 0.0);
        
        vec3 baseColor;
        
        if (uUseFoliageColor) {
            // РљСЂРѕРЅР°
            baseColor = uFoliageColor;
            
            // Р”РѕР±Р°РІР»СЏРµРј РЅРµР±РѕР»СЊС€СѓСЋ РІР°СЂРёР°С†РёСЋ РґР»СЏ РѕР±СЉРµРјР°
            float leafVar = 0.85 + (sin(vUV.x * 20.0 + vUV.y * 30.0) * 0.15);
            baseColor = baseColor * leafVar;
            
            // РџРѕРґСЃРІРµС‚РєР° РІРµСЂС…СѓС€РµРє
            if (vUV.y > 0.7) {
                float highlight = (vUV.y - 0.7) * 1.5;
                baseColor += vec3(0.15, 0.1, 0.05) * highlight;
            }
        } else {
            // РЎС‚РІРѕР»
            baseColor = uTrunkColor;
            
            // РўРµРєСЃС‚СѓСЂР° РєРѕСЂС‹
            float barkVar = 0.8 + (sin(vUV.x * 50.0) * 0.2);
            baseColor = baseColor * barkVar;
        }
        
        // РћР РР“РРќРђР›Р¬РќРћР• РћРЎР’Р•Р©Р•РќРР• РёР· РїРµСЂРІРѕР№ РІРµСЂСЃРёРё
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

