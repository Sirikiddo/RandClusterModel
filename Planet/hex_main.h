#pragma once
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <glew.h>
#include "hex.h"
#include <utility>
#include <map>
#include <glfw3.h>


struct AppData {
    HexGrid* grid = nullptr;
    std::map<std::pair<int, int>, int> hexClickCount;
    std::vector<std::pair<int, int>> path;
    GLuint textVAO = 0, textVBO = 0;
    float rotateX = 0.0f;
    float rotateY = 0.0f;
    bool isRotating = false;
    double lastX = 0, lastY = 0;
};

void mouseButtonCallbackHex(GLFWwindow* window, int button, int action, int mods) { //
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        AppData* appData = static_cast<AppData*>(glfwGetWindowUserPointer(window));
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        float worldX = static_cast<float>(xpos);
        float worldY = static_cast<float>(600 - ypos);

        auto hexCoords = appData->grid->getHexAtPosition(worldX, worldY);
        if (hexCoords.first == -1) return;

        auto& count = appData->hexClickCount[hexCoords];
        count = (count + 1) % 4;

        if (count == 2) {
            static std::pair<int, int> firstHex = { -1, -1 };
            if (firstHex.first == -1) {
                firstHex = hexCoords;
            }
            else {
                auto path = appData->grid->findPath(firstHex, hexCoords, appData->hexClickCount);
                appData->path = path;
                firstHex = { -1, -1 };
            }
        }
    }
}


void renderText(GLFWwindow* window, const std::string& text, float x, float y, float scale) {
    AppData* appData = static_cast<AppData*>(glfwGetWindowUserPointer(window));

    // Простой рендеринг текста с помощью линий
    glBindVertexArray(appData->textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, appData->textVBO);

    std::vector<float> vertices;
    for (char c : text) {
        // Простейшая геометрия для цифр (пример для '0')
        switch (c) {
        case '0': /* координаты линий для 0 */ break;
        case '1': /* координаты для 1 */ break;
            // ... Добавьте геометрию для всех нужных символов
        }
        // Добавьте смещение для следующего символа
        x += 15.0f * scale;
    }

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, vertices.size() / 2);
}

int HexMain() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return -1;
    }

    // Íàñòðîéêà êîíòåêñòà OpenGL
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

    // Øåéäåðû
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

    // Êîìïèëÿöèÿ øåéäåðîâ
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

    // Ïðîâåðêà îøèáîê
    GLint success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader program error: " << infoLog << "\n";
    }

    // Ïðîåêöèîííàÿ ìàòðèöà
    glm::mat4 projection = glm::ortho(0.0f, 800.0f, 0.0f, 600.0f, -1.0f, 1.0f);
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));


    // Èíèöèàëèçàöèÿ ñåòêè
    HexGrid grid(10, 10, 30.0f, 800.0f, 600.0f); // Óâåëè÷üòå ðàçìåð ãåêñà äëÿ ëó÷øåé âèäèìîñòè
    AppData appData;
    appData.grid = &grid;
    glfwSetWindowUserPointer(window, &appData);
    glfwSetMouseButtonCallback(window, mouseButtonCallbackHex); //

    // Íàñòðîéêà áóôåðîâ
    GLuint VAO, VBO, EBO, EBO_lines;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glGenBuffers(1, &EBO_lines);

    glBindVertexArray(VAO);

    // VBO äëÿ âåðøèí
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, grid.getVertices().size() * sizeof(Vec2), grid.getVertices().data(), GL_STATIC_DRAW);

    // EBO äëÿ òðåóãîëüíèêîâ
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, grid.getIndices().size() * sizeof(unsigned int), grid.getIndices().data(), GL_STATIC_DRAW);

    // EBO äëÿ ëèíèé
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_lines);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, grid.getLineIndices().size() * sizeof(unsigned int), grid.getLineIndices().data(), GL_STATIC_DRAW);

    // Íàñòðîéêà àòðèáóòîâ
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Ãëàâíûé öèêë
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        // Ðèñóåì áåëûå øåñòèóãîëüíèêè
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 1.0f, 1.0f, 1.0f, 1.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glDrawElements(GL_TRIANGLES, grid.getIndices().size(), GL_UNSIGNED_INT, 0);

        // Ðèñóåì ÷åðíûå âûáðàííûå øåñòèóãîëüíèêè
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 0.0f, 0.0f, 0.0f, 1.0f);
        glBindVertexArray(VAO); // Âêëþ÷àåì VAO ïåðåä îòðèñîâêîé!

        // Рисуем выбранные гексы с разными цветами
        for (const auto& entry : appData.hexClickCount) {
            const auto& hexCoords = entry.first;
            int clickCount = entry.second;

            int col = hexCoords.first;
            int row = hexCoords.second;

            if (col < 0 || col >= grid.getWidth() || row < 0 || row >= grid.getHeight()) {
                continue;
            }

            glm::vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
            switch (clickCount) {
            case 1: color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); break;
            case 2: color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); break;
            case 3: color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); break;
            default: continue;
            }

            glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), color.r, color.g, color.b, color.a);
            int hexIndex = row * grid.getWidth() + col;
            int baseIndex = hexIndex * 18;
            glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, (void*)(baseIndex * sizeof(unsigned int)));
        } // Закрывающая скобка для цикла по hexClickCount

        // Рисуем путь
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 1.0f, 0.0f, 0.0f, 1.0f);
        for (const auto& hex : appData.path) {
            int hexIndex = hex.second * grid.getWidth() + hex.first;
            int baseIndex = hexIndex * 18;
            glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, (void*)(baseIndex * sizeof(unsigned int)));
        }

        // Рисуем контуры
        glUniform4f(glGetUniformLocation(shaderProgram, "uColor"), 0.0f, 0.0f, 0.0f, 1.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_lines);
        glDrawElements(GL_LINES, grid.getLineIndices().size(), GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    } // Закрывающая скобка для основного цикла while

    // Очистка ресурсов
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteBuffers(1, &EBO_lines);
    glfwTerminate();

    return 0;
}