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
    // Мировые координаты вершины
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Нормаль трансформируем корректно (матрицей без масштабирования)
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

uniform vec3 uLightDir;   // направление света в мировых координатах
uniform vec3 uViewPos;    // позиция камеры (для будущего specular)

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);  // если uLightDir — это направление света, а не вектор к источнику

    // diffuse по Ламберту
    float diff = max(dot(N, L), 0.0);

    // добавим немного ambient, чтобы не было полностью чёрных теней
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

// Шум для более сложных волн
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
    // Генерация текстурных координат из позиции
    vTexCoord = aPos.xz * 2.0;
    
    // Многослойный шум для сложных волн
    float wave1 = sin(aPos.x * 4.0 + uTime * 1.5) * 0.02;
    float wave2 = sin(aPos.z * 3.0 + uTime * 1.2) * 0.015;
    
    // Добавляем шум Перлина для более естественных волн
    float noiseWave = noise(vec2(aPos.x * 3.0 + uTime * 0.8, aPos.z * 3.0)) * 0.01;
    float noiseWave2 = noise(vec2(aPos.x * 6.0 - uTime * 0.6, aPos.z * 6.0)) * 0.005;
    
    // Комбинируем все волны
    float totalWave = wave1 + wave2 + noiseWave + noiseWave2;
    
    // Смещение по нормали (вверх)
    vec3 displaced = aPos + vec3(0.0, totalWave, 0.0);
    vWorldPos = displaced;
    
    // Вычисление нормали для волн
    float h = 0.01;
    float dx = noise(vec2((aPos.x + h) * 3.0 + uTime * 0.8, aPos.z * 3.0)) - 
               noise(vec2((aPos.x - h) * 3.0 + uTime * 0.8, aPos.z * 3.0));
    float dz = noise(vec2(aPos.x * 3.0 + uTime * 0.8, (aPos.z + h) * 3.0)) - 
               noise(vec2(aPos.x * 3.0 + uTime * 0.8, (aPos.z - h) * 3.0));
    
    vNormal = normalize(vec3(-dx * 2.0, 1.0, -dz * 2.0));
    
    gl_Position = uMVP * vec4(displaced, 1.0);
}
)GLSL";

// ─── Water Fragment Shader (ИСПРАВЛЕННЫЙ) ───────────────────────────────────
// ─── Water Fragment Shader (ИСПРАВЛЕННЫЙ ЦВЕТ) ───────────────────────────────────
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

// Шумовые функции
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
    
    // КРАСИВЫЙ СИНИЙ ЦВЕТ ВОДЫ С ПЕРЕЛИВАНИЕМ
    float depth = max(0.0, 1.0 - length(vWorldPos) * 0.8);
    
    // Основные цвета воды - глубокий синий с изумрудными оттенками
    vec3 shallowColor = vec3(0.1, 0.4, 0.8);   // Бирюзово-синий для мелководья
    vec3 deepColor = vec3(0.0, 0.15, 0.4);     // Глубокий синий
    
    // Добавляем легкие переливания цвета на основе шума
    float colorVariation = fbm(vTexCoord * 0.5 + uTime * 0.1) * 0.2 - 0.1;
    shallowColor.r += colorVariation * 0.1;
    shallowColor.g += colorVariation * 0.2;
    
    vec3 waterColor = mix(deepColor, shallowColor, depth);
    
    // ОТРАЖЕНИЯ С ФРЕНЕЛЕМ
    vec3 reflectDir = reflect(-viewDir, normal);
    vec3 reflection = texture(uEnvMap, reflectDir).rgb;
    
    // Уменьшаем яркость отражений, чтобы не было белого
    reflection *= 0.6;
    
    // Улучшенный френель
    float fresnelFactor = fresnel(normal, viewDir);
    
    // ОСВЕЩЕНИЕ - уменьшаем интенсивность чтобы не было белого
    vec3 lightDir = normalize(-uLightDir);
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Диффузное освещение с СИНИМ оттенком
    vec3 diffuse = diff * waterColor * 0.4; // Уменьшено с 0.6
    
    // СПЕКУЛЯРНЫЕ БЛИКИ - СИНИЕ вместо белых
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 128.0); // Более резкие блики
    
    // СИНИЕ блики вместо белых
    vec3 specularColor = vec3(0.3, 0.5, 1.0); // Голубовато-синий
    vec3 specular = spec * specularColor * 0.6; // Уменьшена интенсивность
    
    // ПОДПОВЕРХНОСТНОЕ РАССЕЯНИЕ - СИНЕЕ
    vec3 subsurface = vec3(0.0, 0.15, 0.3) * (1.0 - diff) * 0.2;
    
    // КОМБИНИРОВАНИЕ - сохраняем синий цвет
    vec3 baseColor = waterColor + diffuse + subsurface;
    
    // Смешиваем с отражениями по френелю (меньше отражений)
    vec3 finalColor = mix(baseColor, reflection, fresnelFactor * 0.4); // Уменьшено с 0.7
    
    // Добавляем синие блики
    finalColor += specular;
    
    // ЛЕГКАЯ ТЕКСТУРА ПОВЕРХНОСТИ для переливов
    float surfaceNoise = fbm(vTexCoord * 2.0 + uTime * 0.2) * 0.15 + 0.85;
    finalColor *= surfaceNoise;
    
    // Добавляем легкие цветовые вариации для красоты
    float hueShift = sin(uTime * 0.5 + vTexCoord.x * 3.0) * 0.05;
    finalColor.g += hueShift * 0.1;
    finalColor.b += hueShift * 0.05;
    
    // ПРОЗРАЧНОСТЬ С ГЛУБИНОЙ
    float alpha = 0.8 + depth * 0.15 + fresnelFactor * 0.05;
    
    // Гарантируем, что цвет остается в синих тонах
    finalColor = clamp(finalColor, 0.0, 1.0);
    
    FragColor = vec4(finalColor, alpha);
}
)GLSL";

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
uniform bool uUseTexture = false;

void main() {
    // Простая затенённая визуализация
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    
    float diff = max(dot(N, L), 0.0);
    vec3 ambient = 0.3 * uColor;
    vec3 diffuse = 0.7 * diff * uColor;
    
    FragColor = vec4(ambient + diffuse, 1.0);
}
)GLSL";

// ─── Жизненный цикл ─────────────────────────────────────────────────────────────
