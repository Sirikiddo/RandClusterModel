#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <glew.h>
#include <glfw3.h>
#include <iostream>

struct AppData {
    float rotateX = 0.0f;
    float rotateY = 0.0f;
    bool isRotating = false;
    double lastX = 0, lastY = 0;
    glm::vec3 cubePosition; // Позиция кубика в мировых координатах
    bool cubeVisible = false; // Флаг видимости кубика
    glm::vec3 rayEnd; // Конец луча для отрисовки
    glm::mat4 planeModelMatrix = glm::mat4(1.0f);
};

void checkOpenGLError(const char* operation) {
    for (GLenum error = glGetError(); error; error = glGetError()) {
        std::cerr << "OpenGL error after " << operation << ": " << error << std::endl;
    }
}

void raycastToPlane(GLFWwindow* window, double xpos, double ypos, AppData* data) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    // Преобразование координат мыши в NDC
    float x = (2.0f * xpos) / width - 1.0f;
    float y = 1.0f - (2.0f * ypos) / height;

    // Матрицы проекции и вида (как в основном цикле)
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 3.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);

    // Обратная матрица: proj * view * model (плоскости)
    glm::mat4 invMVP = glm::inverse(proj * view * data->planeModelMatrix);

    // Вычисление луча в локальных координатах плоскости
    glm::vec4 rayStart = invMVP * glm::vec4(x, y, -1.0f, 1.0f);
    glm::vec4 rayEnd = invMVP * glm::vec4(x, y, 1.0f, 1.0f);
    rayStart /= rayStart.w;
    rayEnd /= rayEnd.w;

    // Направление луча в локальных координатах плоскости
    glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd - rayStart));

    // Пересечение с локальной плоскостью (Z=0)
    float t = -rayStart.z / rayDir.z;
    glm::vec3 localIntersection = glm::vec3(rayStart) + rayDir * t;

    // Преобразование обратно в мировые координаты
    glm::vec4 worldIntersection = data->planeModelMatrix * glm::vec4(localIntersection, 1.0f);

    data->cubePosition = glm::vec3(worldIntersection);
    data->cubeVisible = true;
    data->rayEnd = glm::vec3(worldIntersection);
}

GLuint planeVAO, planeVBO, planeEBO;
GLuint cubeVAO, cubeVBO, cubeEBO;
GLuint rayVAO, rayVBO;

void myMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    AppData* data = static_cast<AppData*>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        raycastToPlane(window, x, y, data);
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
        data->rotateY -= (xpos - data->lastX) * 0.5f; // Invert X axis for natural rotation
        data->rotateX -= (data->lastY - ypos) * 0.5f; // Invert Y axis for natural rotation
        data->lastX = xpos;
        data->lastY = ypos;
    }
}

void initPlane() {
    // Вершины плоскости (2D квадрат)
    float vertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };

    // Индексы для отрисовки двух треугольников
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

void initCube() {
    // Вершины куба
    float vertices[] = {
        -0.1f, -0.1f, -0.1f,
         0.1f, -0.1f, -0.1f,
         0.1f,  0.1f, -0.1f,
         0.1f,  0.1f, -0.1f,
         0.1f,  0.1f,  0.1f,
        -0.1f,  0.1f,  0.1f,
        -0.1f, -0.1f, -0.1f,
        -0.1f, -0.1f,  0.1f,
        -0.1f,  0.1f,  0.1f,
        -0.1f, -0.1f,  0.1f,
         0.1f, -0.1f,  0.1f,
         0.1f, -0.1f, -0.1f,
         0.1f,  0.1f,  0.1f,
        -0.1f,  0.1f,  0.1f,
        -0.1f,  0.1f, -0.1f,
         0.1f, -0.1f,  0.1f,
         0.1f,  0.1f,  0.1f,
         0.1f, -0.1f, -0.1f,
         0.1f,  0.1f, -0.1f,
         0.1f,  0.1f,  0.1f,
        -0.1f,  0.1f, -0.1f,
        -0.1f,  0.1f,  0.1f,
        -0.1f, -0.1f,  0.1f,
        -0.1f, -0.1f, -0.1f
    };

    // Индексы для отрисовки куба
    unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0,
        4, 5, 6,
        6, 7, 4,
        8, 9, 10,
        10, 11, 8,
        12, 13, 14,
        14, 15, 12,
        16, 17, 18,
        18, 19, 16,
        20, 21, 22,
        22, 23, 20
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
    // Вершины для отрисовки луча
    float vertices[] = {
        0.0f, 0.0f, 0.0f, // Начало луча (камера)
        0.0f, 0.0f, 0.0f  // Конец луча (обновляется при клике)
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

    GLFWwindow* window = glfwCreateWindow(800, 600, "Rotating Plane", nullptr, nullptr);
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

    initPlane();
    initCube();
    initRay();

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Матрицы вида и проекции
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.0f, 0.0f, 3.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            800.0f / 600.0f,
            0.1f,
            100.0f
        );

        // Матрица модели с вращением
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(appData.rotateX), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(appData.rotateY), glm::vec3(0.0f, 1.0f, 0.0f));

        appData.planeModelMatrix = model;

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // Отрисовка плоскости
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 0.4f, 0.6f, 0.8f);
        glBindVertexArray(planeVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        // Отрисовка кубика, если он видим
        if (appData.cubeVisible) {
            glm::mat4 cubeModel = glm::mat4(1.0f);
            cubeModel = glm::translate(cubeModel, appData.cubePosition);
            cubeModel = appData.planeModelMatrix * cubeModel; // Apply plane's rotation to the cube
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(cubeModel));
            glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 0.0f, 0.0f);
            glBindVertexArray(cubeVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        // Отрисовка луча
        glBindVertexArray(rayVAO);
        float rayVertices[] = {
            0.0f, 0.0f, 0.0f, // Начало луча (камера)
            appData.rayEnd.x, appData.rayEnd.y, appData.rayEnd.z // Конец луча
        };
        glBindBuffer(GL_ARRAY_BUFFER, rayVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rayVertices), rayVertices);
        glUniform3f(glGetUniformLocation(shaderProgram, "objectColor"), 1.0f, 1.0f, 0.0f);
        glDrawArrays(GL_LINES, 0, 2);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка
    glDeleteVertexArrays(1, &planeVAO);
    glDeleteBuffers(1, &planeVBO);
    glDeleteBuffers(1, &planeEBO);
    glDeleteVertexArrays(1, &cubeVAO);
    glDeleteBuffers(1, &cubeVBO);
    glDeleteBuffers(1, &cubeEBO);
    glDeleteVertexArrays(1, &rayVAO);
    glDeleteBuffers(1, &rayVBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
    return 0;
}
