// hex_main_2.h (значительные изменения)
#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <glew.h>
#include <glfw3.h>
#include <iostream>
#include <map>
#include "hex.h"

struct AppData {
    // Для вращения и масштабирования
    float rotateX = 0.0f;
    float rotateY = 0.0f;
    float zoomLevel = -5.0f;
    bool isRotating = false;
    double lastX = 0, lastY = 0;

    // Для кубика и raycasting
    glm::vec3 cubePosition;
    bool cubeVisible = false;
    glm::vec3 rayStart{ 0.0f };   // ← добавили
    glm::vec3 rayEnd{ 0.0f };

    // Для гексов
    HexGrid* hexGrid = nullptr;
    std::map<std::pair<int, int>, int> hexClickCount;
    std::vector<std::pair<int, int>> hexPath;
    bool hitHexGrid = false;

    // Расстояния до плоскостей
    float frontPlaneZ = 0.0f;
    float hexPlaneZ = -1.5f;
};

void checkOpenGLError(const char* operation) {
    for (GLenum error = glGetError(); error; error = glGetError()) {
        std::cerr << "OpenGL error after " << operation << ": " << error << std::endl;
    }
}

void hexScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    AppData* data = static_cast<AppData*>(glfwGetWindowUserPointer(window));
    data->zoomLevel += yoffset * 0.5f;
}

//void processHexIntersection(AppData* data, const glm::vec3& hexIntersection) {
//    // Преобразуем координаты в систему гексов
//    float hexGridX = (hexIntersection.x + 1.0f) * data->hexGrid->getWidth() * data->hexGrid->getHexSize() / 2.0f;
//    float hexGridY = (hexIntersection.y + 1.0f) * data->hexGrid->getHeight() * data->hexGrid->getHexHeight() / 2.0f;
//
//    auto hexCoords = data->hexGrid->getHexAtPosition(hexGridX, hexGridY);
//    if (hexCoords.first != -1) {
//        data->hitHexGrid = true;
//        data->cubePosition = hexIntersection;
//        data->cubeVisible = false;
//        data->rayEnd = hexIntersection;
//
//        // Обработка кликов по гексам
//        auto& count = data->hexClickCount[hexCoords];
//        count = (count + 1) % 4;
//
//        if (count == 2) {
//            static std::pair<int, int> firstHex = { -1, -1 };
//            if (firstHex.first == -1) {
//                firstHex = hexCoords;
//            }
//            else {
//                auto path = data->hexGrid->findPath(firstHex, hexCoords, data->hexClickCount);
//                data->hexPath = path;
//                firstHex = { -1, -1 };
//            }
//        }
//    }
//}
void processHexIntersection(AppData* data, const glm::vec3& hexIntersection) {
    // Преобразуем координаты из [-1, 1] в [0, 2], затем масштабируем
    float hexGridX = (hexIntersection.x + 1.0f) * data->hexGrid->getWidth() * data->hexGrid->getHexSize() / 2.0f;
    float hexGridY = (hexIntersection.y + 1.0f) * data->hexGrid->getHeight() * data->hexGrid->getHexHeight() / 2.0f;

    auto hexCoords = data->hexGrid->getHexAtPosition(hexGridX, hexGridY);
    if (hexCoords.first == -1) {
        data->hitHexGrid = false;
        return;
    }

    data->hitHexGrid = true;
    data->cubeVisible = false;

    // Обработка кликов
    auto& count = data->hexClickCount[hexCoords];
    count = (count + 1) % 4;

    // Удаляем гекс из пути, если он заблокирован (count == 3)
    if (count == 3) {
        data->hexPath.erase(
            std::remove_if(data->hexPath.begin(), data->hexPath.end(),
                [hexCoords](const auto& p) { return p == hexCoords; }),
            data->hexPath.end());
        return;
    }

    // Поиск пути между двумя выбранными гексами (count == 2)
    static std::pair<int, int> firstHex = { -1, -1 };
    if (count == 2) {
        if (firstHex.first == -1) {
            firstHex = hexCoords;
        }
        else {
            // Ищем путь только если оба гекса не заблокированы
            if (data->hexClickCount[firstHex] != 3 && data->hexClickCount[hexCoords] != 3) {
                data->hexPath = data->hexGrid->findPath(firstHex, hexCoords, data->hexClickCount);
            }
            firstHex = { -1, -1 };
        }
    }
    else {
        firstHex = { -1, -1 }; // Сброс, если клик не count == 2
    }
}

// ── 2. raycastToPlanes ───────────────────────────────────────
void raycastToPlanes(GLFWwindow* window, double xpos, double ypos, AppData* data)
{
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    // ── 1.  Координаты курсора → NDC
    float ndcX = 2.0f * static_cast<float>(xpos) / width - 1.0f;
    float ndcY = -2.0f * static_cast<float>(ypos) / height + 1.0f;

    glm::vec4 clipNear(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 clipFar(ndcX, ndcY, 1.0f, 1.0f);

    // ── 2.  View / Projection (с учётом zoomLevel)
    glm::vec3 camPosWorld(0.0f, 0.0f, 5.0f + data->zoomLevel);

    glm::mat4 view = glm::lookAt(
        camPosWorld,
        glm::vec3(0, 0, 0),
        glm::vec3(0, 1, 0));

    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
        static_cast<float>(width) / height,
        0.1f, 100.0f);

    glm::mat4 invVP = glm::inverse(proj * view);

    // ── 3.  Точку на far-plane переводим в мир и строим направление
    glm::vec4 worldFar4 = invVP * clipFar;
    glm::vec3 worldFar = glm::vec3(worldFar4) / worldFar4.w;

    glm::vec3 rayOrigWorld = camPosWorld;                    // ← начало луча = камера!
    glm::vec3 rayDirWorld = glm::normalize(worldFar - rayOrigWorld);

    // ── 4.  В пространство модели (учёт вращений)
    glm::mat4 rot = glm::mat4(1.0f);
    rot = glm::rotate(rot, glm::radians(data->rotateX), glm::vec3(1, 0, 0));
    rot = glm::rotate(rot, glm::radians(data->rotateY), glm::vec3(0, 1, 0));

    glm::mat4 invRot = glm::inverse(rot);

    glm::vec3 rayOrig = glm::vec3(invRot * glm::vec4(rayOrigWorld, 1.0f));
    glm::vec3 rayDir = glm::normalize(glm::vec3(invRot * glm::vec4(rayDirWorld, 0.0f)));

    // ⑤ пересечения в model-space
    float tFront = (data->frontPlaneZ - rayOrig.z) / rayDir.z;
    float tHex = (data->hexPlaneZ - rayOrig.z) / rayDir.z;

    glm::vec3 hitFront = rayOrig + tFront * rayDir;
    glm::vec3 hitHex = rayOrig + tHex * rayDir;

    // учёт сдвига сетки гексов (-1,-1)
    //glm::vec3 hitHexLocal = hitHex - glm::vec3(-1.0f, -1.0f, 0.0f);

    bool frontOk = tFront > 0 &&
        hitFront.x >= -1.0f && hitFront.x <= 1.0f &&
        hitFront.y >= -1.0f && hitFront.y <= 1.0f;

    bool hexOk = tHex > 0 &&
        hitHex.x >= -1.0f && hitHex.x <= 1.0f &&
        hitHex.y >= -1.0f && hitHex.y <= 1.0f;

    data->cubeVisible = false;
    data->hitHexGrid = false;

    if (frontOk && (!hexOk || tFront < tHex))      // ближняя плоскость
    {
        data->cubePosition = hitFront;             // уже в model-space
        data->cubeVisible = true;
        data->rayStart = rayOrig;
        data->rayEnd = hitFront;
    }
    else if (hexOk) {
        data->rayStart = rayOrig;
        data->rayEnd = hitHex;
        processHexIntersection(data, hitHex);   // ← передаём ИМЕННО hitHex
    }
    if (hexOk)                                       // ① сначала пробуем гексы
    {
        data->rayStart = rayOrig;
        data->rayEnd = hitHex;
        processHexIntersection(data, hitHex);
    }
    else if (frontOk)                                // ② иначе — передняя плоскость
    {
        data->cubePosition = hitFront;
        data->cubeVisible = true;
        data->rayStart = rayOrig;
        data->rayEnd = hitFront;
    }
    else                                              // ③ мимо всего
    {
        data->cubeVisible = false;
        data->hitHexGrid = false;
    }
}

void myMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    AppData* data = static_cast<AppData*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        raycastToPlanes(window, x, y, data);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        data->isRotating = true;
        glfwGetCursorPos(window, &data->lastX, &data->lastY);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
        data->isRotating = false;
    }
}

void myCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    AppData* data = static_cast<AppData*>(glfwGetWindowUserPointer(window));
    if (data->isRotating) {
        data->rotateY -= (xpos - data->lastX) * 0.5f;
        data->rotateX += (data->lastY - ypos) * 0.5f;
        data->lastX = xpos;
        data->lastY = ypos;
    }
}

GLuint planeVAO, planeVBO, planeEBO;
GLuint hexVAO, hexVBO, hexEBO, hexLineEBO;
GLuint cubeVAO, cubeVBO, cubeEBO;
GLuint rayVAO, rayVBO;

void initPlane() {
    float vertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };

    unsigned int indices[] = {
        0, 1, 2,
        0, 2, 3
    };

    glGenVertexArrays(1, &planeVAO);
    glGenBuffers(1, &planeVBO);
    glGenBuffers(1, &planeEBO);

    glBindVertexArray(planeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, planeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    checkOpenGLError("plane VAO/VBO/EBO initialization");
}

void initHexGrid(AppData* data) {
    // Создаем сетку гексов (размеры подобраны для параллельности)
    data->hexGrid = new HexGrid(10, 10, 0.1f, 2.0f, 2.0f);

    // Создаем VAO/VBO для гексов
    glGenVertexArrays(1, &hexVAO);
    glGenBuffers(1, &hexVBO);
    glGenBuffers(1, &hexEBO);
    glGenBuffers(1, &hexLineEBO);

    glBindVertexArray(hexVAO);

    // Вершины гексов
    glBindBuffer(GL_ARRAY_BUFFER, hexVBO);
    glBufferData(GL_ARRAY_BUFFER, data->hexGrid->getVertices().size() * sizeof(Vec2),
        data->hexGrid->getVertices().data(), GL_STATIC_DRAW);

    // Индексы для треугольников
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hexEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data->hexGrid->getIndices().size() * sizeof(unsigned int),
        data->hexGrid->getIndices().data(), GL_STATIC_DRAW);

    // Индексы для линий
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hexLineEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data->hexGrid->getLineIndices().size() * sizeof(unsigned int),
        data->hexGrid->getLineIndices().data(), GL_STATIC_DRAW);

    // Атрибуты вершин
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    checkOpenGLError("hex grid VAO/VBO/EBO initialization");
}

void initCube() {
    float vertices[] = {
        -0.05f, -0.05f, -0.05f,
         0.05f, -0.05f, -0.05f,
         0.05f,  0.05f, -0.05f,
         0.05f,  0.05f,  0.05f,
        -0.05f,  0.05f,  0.05f,
        -0.05f, -0.05f, -0.05f,
        -0.05f, -0.05f,  0.05f,
        -0.05f,  0.05f,  0.05f,
        -0.05f, -0.05f,  0.05f,
         0.05f, -0.05f,  0.05f,
         0.05f, -0.05f, -0.05f,
         0.05f,  0.05f,  0.05f,
        -0.05f,  0.05f,  0.05f,
        -0.05f,  0.05f, -0.05f,
         0.05f, -0.05f,  0.05f,
         0.05f,  0.05f,  0.05f,
         0.05f, -0.05f, -0.05f,
         0.05f,  0.05f, -0.05f,
         0.05f,  0.05f,  0.05f,
        -0.05f,  0.05f, -0.05f,
        -0.05f,  0.05f,  0.05f,
        -0.05f, -0.05f,  0.05f,
        -0.05f, -0.05f, -0.05f
    };

    unsigned int indices[] = {
        0, 1, 2, 2, 3, 4, 4, 5, 0,
        6, 7, 8, 8, 9, 10, 10, 11, 6,
        12, 13, 14, 14, 15, 16, 16, 17, 12,
        18, 19, 20, 20, 21, 22, 22, 23, 18
    };

    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);
    glGenBuffers(1, &cubeEBO);

    glBindVertexArray(cubeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    checkOpenGLError("cube VAO/VBO/EBO initialization");
}

void initRay() {
    float vertices[] = {
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };

    glGenVertexArrays(1, &rayVAO);
    glGenBuffers(1, &rayVBO);

    glBindVertexArray(rayVAO);

    glBindBuffer(GL_ARRAY_BUFFER, rayVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    checkOpenGLError("ray VAO/VBO initialization");
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "ERROR::SHADER::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    return shader;
}

GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    GLint success;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}

int HexMain() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Hex Grid 3D", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glfwTerminate();
        return -1;
    }

    // Инициализация данных приложения
    AppData appData;
    glfwSetWindowUserPointer(window, &appData);
    glfwSetMouseButtonCallback(window, myMouseButtonCallback);
    glfwSetCursorPosCallback(window, myCursorPosCallback);
    glfwSetScrollCallback(window, hexScrollCallback);

    // Шейдеры
    const char* vertexShaderSource = R"glsl(
        #version 120
        attribute vec3 aPos;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )glsl";

    const char* fragmentShaderSource = R"glsl(
        #version 120
        uniform vec3 objectColor;
        void main() {
            gl_FragColor = vec4(objectColor, 1.0);
        }
    )glsl";

    GLuint shaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource);

    // Инициализация объектов
    initPlane();
    initHexGrid(&appData);
    initCube();
    initRay();

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window))
    {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);

        // Настройка матриц
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 5.0f + appData.zoomLevel),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(width) / height,
            0.1f, 100.0f
        );

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(appData.rotateX), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(appData.rotateY), glm::vec3(0, 1, 0));

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // 1. Отрисовка передней плоскости (голубая)
        glm::mat4 frontModel = glm::translate(model, glm::vec3(0.0f, 0.0f, appData.frontPlaneZ));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(frontModel));
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 0.4f, 0.6f, 0.8f);
        glBindVertexArray(planeVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // 2. Отрисовка гексов
        glm::mat4 hexModel = glm::translate(model, glm::vec3(-1.0f, -1.0f, appData.hexPlaneZ));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(hexModel));
        glBindVertexArray(hexVAO);

        // 2.1. Основная белая заливка всех гексов
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 1.0f, 1.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hexEBO);
        glDrawElements(GL_TRIANGLES, appData.hexGrid->getIndices().size(), GL_UNSIGNED_INT, 0);

        // 2.2. Отрисовка выбранных гексов (поверх основной заливки)
        for (const auto& entry : appData.hexClickCount) {
            const auto& coords = entry.first;
            int count = entry.second;

            if (coords.first < 0 || coords.second < 0 ||
                coords.first >= appData.hexGrid->getWidth() ||
                coords.second >= appData.hexGrid->getHeight()) {
                continue;
            }

            glm::vec3 color;
            switch (count) {
            case 1: color = glm::vec3(0.0f, 0.0f, 0.0f); break; // Чёрный
            case 2: color = glm::vec3(0.0f, 1.0f, 0.0f); break; // Зелёный
            case 3: color = glm::vec3(1.0f, 0.0f, 0.0f); break; // Красный
            default: continue;
            }

            glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), color.r, color.g, color.b);
            int hexIndex = coords.second * appData.hexGrid->getWidth() + coords.first;
            glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, (void*)(hexIndex * 18 * sizeof(unsigned int)));
        }

        // 2.3. Отрисовка пути (если есть)
        if (!appData.hexPath.empty()) {
            glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 0.5f, 0.0f);
            for (const auto& hex : appData.hexPath) {
                int hexIndex = hex.second * appData.hexGrid->getWidth() + hex.first;
                glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, (void*)(hexIndex * 18 * sizeof(unsigned int)));
            }
        }

        // 2.4. Границы гексов (поверх всего)
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 0.0f, 0.0f, 0.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hexLineEBO);
        glDrawElements(GL_LINES, appData.hexGrid->getLineIndices().size(), GL_UNSIGNED_INT, 0);

        // 3. Отрисовка куба (если виден)
        if (appData.cubeVisible) {
            glm::mat4 cubeModel = glm::translate(model, appData.cubePosition);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(cubeModel));
            glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 0.0f, 0.0f);
            glBindVertexArray(cubeVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        // 4. Отрисовка луча
        {
            float rayVerts[6] = {
                appData.rayStart.x, appData.rayStart.y, appData.rayStart.z,
                appData.rayEnd.x, appData.rayEnd.y, appData.rayEnd.z
            };

            glBindVertexArray(rayVAO);
            glBindBuffer(GL_ARRAY_BUFFER, rayVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rayVerts), rayVerts);

            glm::mat4 rayModel = glm::mat4(1.0f);
            rayModel = glm::rotate(rayModel, glm::radians(appData.rotateX), glm::vec3(1, 0, 0));
            rayModel = glm::rotate(rayModel, glm::radians(appData.rotateY), glm::vec3(0, 1, 0));

            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"),
                1, GL_FALSE, glm::value_ptr(rayModel));
            glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 1.0f, 0.0f);

            glDrawArrays(GL_LINES, 0, 2);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка
    glDeleteVertexArrays(1, &planeVAO);
    glDeleteBuffers(1, &planeVBO);
    glDeleteBuffers(1, &planeEBO);
    glDeleteVertexArrays(1, &hexVAO);
    glDeleteBuffers(1, &hexVBO);
    glDeleteBuffers(1, &hexEBO);
    glDeleteBuffers(1, &hexLineEBO);
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &cubeEBO);
    glDeleteVertexArrays(1, &rayVAO);
    glDeleteBuffers(1, &rayVBO);
    glDeleteProgram(shaderProgram);
    delete appData.hexGrid;
    glfwTerminate();
    return 0;
}