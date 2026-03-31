#pragma once

// ─── Шейдеры ───────────────────────────────────────────────────────────────────
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

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix; 

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_TERRAIN = R"GLSL(
#version 330 core
in vec3 vColor;
in vec3 vNormal;
in vec3 vWorldPos;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = 0.3 * vColor;
    vec3 diffuse = 0.7 * diff * vColor;
    vec3 result = ambient + diffuse;
    FragColor = vec4(result, 1.0);
}
)GLSL";

static const char* FS_SEL = R"GLSL(
#version 330 core
out vec4 FragColor;
void main(){ FragColor = vec4(1.0,1.0,0.2,1.0); }
)GLSL";

// ─── Water shader ─────────────────────────────────────────────────────────────
static const char* VS_WATER = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vTexCoord = aPos.xz * 2.0;
    
    float wave1 = sin(aPos.x * 4.0 + uTime * 1.5) * 0.02;
    float wave2 = sin(aPos.z * 3.0 + uTime * 1.2) * 0.015;
    
    float noiseWave = noise(vec2(aPos.x * 3.0 + uTime * 0.8, aPos.z * 3.0)) * 0.01;
    float noiseWave2 = noise(vec2(aPos.x * 6.0 - uTime * 0.6, aPos.z * 6.0)) * 0.005;
    
    float totalWave = wave1 + wave2 + noiseWave + noiseWave2;
    
    vec3 displaced = aPos + vec3(0.0, totalWave, 0.0);
    vWorldPos = displaced;
    
    float h = 0.01;
    float dx = noise(vec2((aPos.x + h) * 3.0 + uTime * 0.8, aPos.z * 3.0)) - 
               noise(vec2((aPos.x - h) * 3.0 + uTime * 0.8, aPos.z * 3.0));
    float dz = noise(vec2(aPos.x * 3.0 + uTime * 0.8, (aPos.z + h) * 3.0)) - 
               noise(vec2(aPos.x * 3.0 + uTime * 0.8, (aPos.z - h) * 3.0));
    
    vNormal = normalize(vec3(-dx * 2.0, 1.0, -dz * 2.0));
    
    gl_Position = uMVP * vec4(displaced, 1.0);
}
)GLSL";

static const char* FS_WATER = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform float uTime;
uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform samplerCube uEnvMap;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

float fresnel(vec3 normal, vec3 viewDir) {
    return pow(1.0 - max(dot(normal, viewDir), 0.0), 4.0);
}

void main() {
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 normal = normalize(vNormal);
    
    float depth = max(0.0, 1.0 - length(vWorldPos) * 0.8);
    
    vec3 shallowColor = vec3(0.1, 0.4, 0.8);
    vec3 deepColor = vec3(0.0, 0.15, 0.4);
    
    float colorVariation = fbm(vTexCoord * 0.5 + uTime * 0.1) * 0.2 - 0.1;
    shallowColor.r += colorVariation * 0.1;
    shallowColor.g += colorVariation * 0.2;
    
    vec3 waterColor = mix(deepColor, shallowColor, depth);
    
    vec3 reflectDir = reflect(-viewDir, normal);
    vec3 reflection = texture(uEnvMap, reflectDir).rgb;
    reflection *= 0.6;
    
    float fresnelFactor = fresnel(normal, viewDir);
    
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(normal, lightDir), 0.0);
    
    vec3 diffuse = diff * waterColor * 0.4;
    
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 128.0);
    
    vec3 specularColor = vec3(0.3, 0.5, 1.0);
    vec3 specular = spec * specularColor * 0.6;
    
    vec3 subsurface = vec3(0.0, 0.15, 0.3) * (1.0 - diff) * 0.2;
    
    vec3 baseColor = waterColor + diffuse + subsurface;
    vec3 finalColor = mix(baseColor, reflection, fresnelFactor * 0.4);
    finalColor += specular;
    
    float surfaceNoise = fbm(vTexCoord * 2.0 + uTime * 0.2) * 0.15 + 0.85;
    finalColor *= surfaceNoise;
    
    float hueShift = sin(uTime * 0.5 + vTexCoord.x * 3.0) * 0.05;
    finalColor.g += hueShift * 0.1;
    finalColor.b += hueShift * 0.05;
    
    float alpha = 0.8 + depth * 0.15 + fresnelFactor * 0.05;
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    FragColor = vec4(finalColor, alpha);
}
)GLSL";

// --- МОДЕЛЬНЫЙ ШЕЙДЕР (ТОЛЬКО ОДИН РАЗ!) ---
static const char* VS_MODEL = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_MODEL = R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUV;

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform vec3 uColor;
uniform bool uUseTexture;
uniform int uIsCar;
uniform sampler2D uTexture;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    
    float diff = max(dot(N, L), 0.0);
    
    vec3 baseColor = uColor;
    const vec3 carYellow = vec3(1.0, 0.85, 0.05);
    if (uIsCar == 1) {
        // Для машины сохраняем рисунок текстуры, но делаем непрозрачно-жёлтый вид:
        // - альфу текстуры НЕ используем в итоговом цвете (иначе будет "прозрачность/дырки")
        // - если текстура premultiplied, восстанавливаем rgb через rgb/a
        // - затем тонируем в жёлтый через luminance-маску
        if (uUseTexture) {
            vec4 tex = texture(uTexture, vUV);
            float a = max(tex.a, 1e-3);
            vec3 texRGB = clamp(tex.rgb / a, 0.0, 1.0);
            float lum = dot(texRGB, vec3(0.2126, 0.7152, 0.0722));
            // Убираем "провалы" в ноль (тёмные пиксели текстуры делали участок чёрным/неокрашенным).
            float lumClamped = clamp(lum, 0.0, 1.0);
            const float lumMin = 0.6; // гарантируем минимум ярко-жёлтого (чтобы верх не уходил в "черно")
            lumClamped = lumMin + (1.0 - lumMin) * lumClamped;
            baseColor = carYellow * lumClamped;
        } else {
            baseColor = carYellow;
        }
    }

    vec3 finalColor = baseColor;
    
    vec3 ambient = 0.3 * finalColor;
    vec3 diffuse = 0.7 * diff * finalColor;

    // Specular-блик только для машины (делает покрытие "реальным").
    vec3 specular = vec3(0.0);
    if (uIsCar == 1) {
        vec3 H = normalize(L + V);
        float ndh = max(dot(N, H), 0.0);
        float shininess = 64.0;
        float carSpec = pow(ndh, shininess);
        specular = 0.45 * carSpec * vec3(1.0, 1.0, 0.9);
    }

    vec3 outColor = ambient + diffuse + specular;

    FragColor = vec4(outColor, 1.0);
}
)GLSL";