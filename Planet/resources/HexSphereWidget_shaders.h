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

