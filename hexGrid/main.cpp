#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <glew.h>
#include <glfw3.h>
#include "hex.h"
#include <iostream>
#include <utility>

struct AppData {
    HexGrid* grid;
    std::vector<std::pair<int, int>> selectedHexes;
};

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        AppData* appData = static_cast<AppData*>(glfwGetWindowUserPointer(window));
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        // Конвертация координат окна в мировые (с инверсией Y)
        float worldX = static_cast<float>(xpos);
        float worldY = static_cast<float>(600 - ypos); // Инверсия Y

        std::pair<int, int> hexCoords = appData->grid->getHexAtPosition(worldX, worldY);
        if (hexCoords.first != -1 && hexCoords.second != -1) {
            std::cout << "Selected hex: (" << hexCoords.first << ", " << hexCoords.second << ")\n";
            appData->selectedHexes.push_back(hexCoords);
        }
    }
}

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    // Настройка контекста OpenGL
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Hex Grid", nullptr, nullptr);
    if (!window) {
        std::cerr << "Window creation failed\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW init failed\n";
        glfwTerminate();
        return -1;
    }

    // Шейдеры
    const char* vertexShaderSource = R"glsl(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        uniform mat4 projection;
        void main() {
            gl_Position = projection * vec4(aPos, 0.0, 1.0);
        }
    )glsl";

    const char* fragmentShaderSource = R"glsl(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 uColor;
        void main() {
            FragColor = uColor;
        }
    )glsl";

    // Компиляция шейдеров
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Проверка ошибок
    GLint success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader program error: " << infoLog << "\n";
    }

    // Проекционная матрица
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 0.0f, 600.0f, -1.0f, 1.0f);
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));


    // Инициализация сетки
    HexGrid grid(10, 10, 30.0f, 800.0f, 600.0f); // Увеличьте размер гекса для лучшей видимости
    AppData appData;
    appData.grid = &grid;
    glfwSetWindowUserPointer(window, &appData);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    // Настройка буферов
    GLuint VAO, VBO, EBO, EBO_lines;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glGenBuffers(1, &EBO_lines);

    glBindVertexArray(VAO);

    // VBO для вершин
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, grid.getVertices().size() * sizeof(Vec2), grid.getVertices().data(), GL_STATIC_DRAW);

    // EBO для треугольников
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, grid.getIndices().size() * sizeof(unsigned int), grid.getIndices().data(), GL_STATIC_DRAW);

    // EBO для линий
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_lines);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, grid.getLineIndices().size() * sizeof(unsigned int), grid.getLineIndices().data(), GL_STATIC_DRAW);

    // Настройка атрибутов
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Главный цикл
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        // Рисуем белые шестиугольники
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 1.0f, 1.0f, 1.0f, 1.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glDrawElements(GL_TRIANGLES, grid.getIndices().size(), GL_UNSIGNED_INT, 0);

        // Рисуем черные выбранные шестиугольники
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 0.0f, 0.0f, 0.0f, 1.0f);
        glBindVertexArray(VAO); // Включаем VAO перед отрисовкой!

        for (const auto& hex : appData.selectedHexes) {
            int col = hex.first;
            int row = hex.second;

            if (col < 0 || col >= grid.getWidth() || row < 0 || row >= grid.getHeight()) {
                continue; // Пропускаем неверные индексы
            }

            int hexIndex = row * grid.getWidth() + col;
            int baseIndex = hexIndex * 18; // 18 индексов на один гекс (6 треугольников)

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, (void*)(baseIndex * sizeof(unsigned int)));
        }


        // Рисуем контуры
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 0.0f, 0.0f, 0.0f, 1.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_lines);
        glDrawElements(GL_LINES, grid.getLineIndices().size(), GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteBuffers(1, &EBO_lines);
    glfwTerminate();

    return 0;
}