#include <iostream>
#include "iconsider.h"
#include "hex_main.h"

#include <glew.h>
#include <glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <iostream>
#include <map>


// Константы
const int WIDTH = 800;
const int HEIGHT = 600;

// Структура для кнопки
struct Button {
    float x, y, width, height;
    std::string label;
    int (*action)();
};

Button buttons[] = {
    { 300, 300, 200, 50, "HexGrid", HexMain},
    { 300, 200, 200, 50, "Iconsider", IconSider }
};

// Структура для символа FreeType
struct Character {
    unsigned int textureID; // ID текстуры символа
    glm::ivec2   size;      // Размеры символа
    glm::ivec2   bearing;   // Смещение от линии шрифта
    unsigned int advance;   // Расстояние до следующего символа
};

std::map<char, Character> characters;

// Инициализация FreeType
void initFreeType() {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "Ошибка инициализации FreeType" << std::endl;
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, "arial.ttf", 0, &face)) {
        std::cerr << "Ошибка загрузки шрифта" << std::endl;
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, 24);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            continue;
        }

        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)
        };
        characters.insert(std::pair<char, Character>(c, character));
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

// Отрисовка текста
void renderText(const std::string& text, float x, float y, float scale, glm::vec3 color) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto c = text.begin(); c != text.end(); c++) {
        Character ch = characters[*c];

        float xpos = x + ch.bearing.x * scale;
        float ypos = y - (ch.size.y - ch.bearing.y) * scale;

        // Отрисовка символа
        glBindTexture(GL_TEXTURE_2D, ch.textureID);
        glBegin(GL_QUADS);
        glColor3f(color.r, color.g, color.b);
        glTexCoord2f(0, 0); glVertex2f(xpos, ypos);
        glTexCoord2f(1, 0); glVertex2f(xpos + ch.size.x * scale, ypos);
        glTexCoord2f(1, 1); glVertex2f(xpos + ch.size.x * scale, ypos + ch.size.y * scale);
        glTexCoord2f(0, 1); glVertex2f(xpos, ypos + ch.size.y * scale);
        glEnd();

        x += (ch.advance >> 6) * scale; // Сдвиг на расстояние до следующего символа
    }
}

// Проверка клика по кнопке
bool isMouseOverButton(GLFWwindow* window, const Button& btn) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    ypos = HEIGHT - ypos; // Инвертируем Y
    return (xpos >= btn.x && xpos <= btn.x + btn.width &&
        ypos >= btn.y && ypos <= btn.y + btn.height);
}

// Обработчик кликов
void mouseButtonCallbackCur(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        for (auto& btn : buttons) {
            if (isMouseOverButton(window, btn)) {
                int result = btn.action();
                std::cout << "Result: " << result << std::endl;
            }
        }
    }
}


// Отрисовка кнопки
void drawButton(const Button& btn) {
    // Прямоугольник кнопки
    glColor3f(0.8f, 0.8f, 0.8f);
    glBegin(GL_QUADS);
    glVertex2f(btn.x, btn.y);
    glVertex2f(btn.x + btn.width, btn.y);
    glVertex2f(btn.x + btn.width, btn.y + btn.height);
    glVertex2f(btn.x, btn.y + btn.height);
    glEnd();

    // Текст кнопки
    renderText(btn.label, btn.x + 20, btn.y + 35, 0.5f, glm::vec3(0.0f, 0.0f, 0.0f));
}

int main() {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "GLFW Buttons", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetMouseButtonCallback(window, mouseButtonCallbackCur);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;

    initFreeType(); // Инициализация FreeType

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIDTH, 0, HEIGHT, -1, 1);
    glMatrixMode(GL_MODELVIEW);

    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);

        // Отрисовка кнопок
        for (const auto& btn : buttons) {
            drawButton(btn);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
/*int main() {
	// HexMain();
	// IconSider();
	return 0;
}*/