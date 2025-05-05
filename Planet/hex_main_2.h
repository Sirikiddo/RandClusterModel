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
    glm::vec3 rayEnd;

    // Для гексов
    HexGrid* hexGrid = nullptr;
    std::map<std::pair<int, int>, int> hexClickCount;
    std::vector<std::pair<int, int>> hexPath;
    bool hitHexGrid = false;

    // Расстояния до плоскостей
    float frontPlaneZ = 0.0f;
    float hexPlaneZ = -1.5f;

    //glm::vec3 cameraPosition = glm::vec3(0.0f, 0.0f, 5.0f);
};

void checkOpenGLError(const char* operation) {
    for (GLenum error = glGetError(); error; error = glGetError()) {
        std::cerr << "OpenGL error after " << operation << ": " << error << std::endl;
    }
}

void hexScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    AppData* data = static_cast<AppData*>(glfwGetWindowUserPointer(window));
    data->zoomLevel += yoffset * 0.5f;
    //data->cameraPosition.z = 5.0f + data->zoomLevel; // Обновляем позицию камеры
}

void processHexIntersection(AppData* data, const glm::vec3& hexIntersection) {
    // Преобразуем координаты в систему гексов
    float hexGridX = (hexIntersection.x + 1.0f) * data->hexGrid->getWidth() * data->hexGrid->getHexSize() / 2.0f;
    float hexGridY = (hexIntersection.y + 1.0f) * data->hexGrid->getHeight() * data->hexGrid->getHexHeight() / 2.0f;

    auto hexCoords = data->hexGrid->getHexAtPosition(hexGridX, hexGridY);
    if (hexCoords.first != -1) {
        data->hitHexGrid = true;
        data->cubePosition = hexIntersection;
        data->cubeVisible = true;
        data->rayEnd = hexIntersection;

        // Обработка кликов по гексам
        auto& count = data->hexClickCount[hexCoords];
        count = (count + 1) % 4;

        if (count == 2) {
            static std::pair<int, int> firstHex = { -1, -1 };
            if (firstHex.first == -1) {
                firstHex = hexCoords;
            }
            else {
                auto path = data->hexGrid->findPath(firstHex, hexCoords, data->hexClickCount);
                data->hexPath = path;
                firstHex = { -1, -1 };
            }
        }
    }
}

void raycastToPlanes(GLFWwindow* window, double xpos, double ypos, AppData* data) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    // Преобразование координат мыши в NDC
    float x = (2.0f * xpos) / width - 1.0f;
    float y = 1.0f - (2.0f * ypos) / height;

    // Матрицы проекции и вида
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 5.0f + data->zoomLevel), // Синхронизируем с основной камерой
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);

    // Обратная матрица: proj * view
    glm::mat4 invVP = glm::inverse(proj * view);

    // Вычисление луча в мировых координатах
    glm::vec4 rayStartNDC = glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 rayEndNDC = glm::vec4(x, y, 1.0f, 1.0f);

    glm::vec4 rayStartWorld = invVP * rayStartNDC;
    glm::vec4 rayEndWorld = invVP * rayEndNDC;
    rayStartWorld /= rayStartWorld.w;
    rayEndWorld /= rayEndWorld.w;

    // Направление луча
    glm::vec3 rayDir = glm::normalize(glm::vec3(rayEndWorld - rayStartWorld));

    // Начало луча - позиция камеры (0,0,5 + zoom)
    glm::vec3 rayStart = glm::vec3(0.0f, 0.0f, 5.0f + data->zoomLevel);

    // Проверка пересечения с передней плоскостью
    float tFront = (data->frontPlaneZ - rayStart.z) / rayDir.z;
    glm::vec3 frontIntersection = rayStart + rayDir * tFront;

    // Проверка пересечения с плоскостью гексов
    float tHex = (data->hexPlaneZ - rayStart.z) / rayDir.z;
    glm::vec3 hexIntersection = rayStart + rayDir * tHex;

    data->hitHexGrid = false;
    data->cubeVisible = false;

    // Проверяем, попадает ли пересечение в границы передней плоскости
    bool hitFrontPlane = (tFront > 0) &&
        (frontIntersection.x >= -1.0f && frontIntersection.x <= 1.0f) &&
        (frontIntersection.y >= -1.0f && frontIntersection.y <= 1.0f);

    // Проверяем, попадает ли пересечение в границы плоскости гексов
    bool hitHexPlane = (tHex > 0) &&
        (hexIntersection.x >= -1.0f && hexIntersection.x <= 1.0f) &&
        (hexIntersection.y >= -1.0f && hexIntersection.y <= 1.0f);

    if (hitFrontPlane && hitHexPlane) {
        if (tFront < tHex) {
            data->cubePosition = frontIntersection;
            data->cubeVisible = true;
            data->rayEnd = frontIntersection;
        }
        else {
            processHexIntersection(data, hexIntersection);
        }
    }
    else if (hitFrontPlane) {
        data->cubePosition = frontIntersection;
        data->cubeVisible = true;
        data->rayEnd = frontIntersection;
    }
    else if (hitHexPlane) {
        processHexIntersection(data, hexIntersection);
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

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);

        // Матрицы вида и проекции
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 5.0f + appData.zoomLevel), // Добавляем zoomLevel
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        // Применяем зум
        //view = glm::translate(view, glm::vec3(0.0f, 0.0f, appData.zoomLevel));

        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            (float)width / (float)height,
            0.1f,
            100.0f
        );

        // Матрица модели с вращением
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(appData.rotateX), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(appData.rotateY), glm::vec3(0.0f, 1.0f, 0.0f));

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // Отрисовка передней плоскости (голубая)
        glm::mat4 frontModel = glm::translate(model, glm::vec3(0.0f, 0.0f, appData.frontPlaneZ));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(frontModel));
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 0.4f, 0.6f, 0.8f);
        glBindVertexArray(planeVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glm::mat4 hexModel = glm::translate(model, glm::vec3(-1.0f, -1.0f, appData.hexPlaneZ));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(hexModel));

        // Заливка гексов (белая)
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 1.0f, 1.0f);
        glBindVertexArray(hexVAO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hexEBO);
        glDrawElements(GL_TRIANGLES, appData.hexGrid->getIndices().size(), GL_UNSIGNED_INT, 0);

        // Границы гексов (чёрные)
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 0.0f, 0.0f, 0.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hexLineEBO);
        glDrawElements(GL_LINES, appData.hexGrid->getLineIndices().size(), GL_UNSIGNED_INT, 0);

        // Отрисовка куба (если виден)
        if (appData.cubeVisible) {
            glm::mat4 cubeModel = glm::translate(model, appData.cubePosition);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(cubeModel));
            glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 0.0f, 0.0f);
            glBindVertexArray(cubeVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        // Отрисовка луча (если нужно)
        float rayVertices[] = {
            0.0f, 0.0f, 0.0f,
            appData.rayEnd.x, appData.rayEnd.y, appData.rayEnd.z
        };
        glBindVertexArray(rayVAO);
        glBindBuffer(GL_ARRAY_BUFFER, rayVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rayVertices), rayVertices);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 1.0f, 0.0f);
        glDrawArrays(GL_LINES, 0, 2);

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