#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <GLFW/glfw3.h>

#define M_PI 3.14159265358979323846

// Структура для представления 3D вектора/точки
struct Vector3 {
    float x, y, z;

    // Конструктор с параметрами по умолчанию
    Vector3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

    // Оператор сложения векторов
    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    // Оператор вычитания векторов
    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    // Умножение вектора на скаляр
    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    // Деление вектора на скаляр
    Vector3 operator/(float scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }

    // Скалярное произведение векторов
    float dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    // Векторное произведение
    Vector3 cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,  // x-компонента
            z * other.x - x * other.z,  // y-компонента
            x * other.y - y * other.x   // z-компонента
        );
    }

    // Длина вектора
    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    // Нормализация вектора (приведение к длине 1)
    Vector3 normalize() const {
        float len = length();
        return Vector3(x / len, y / len, z / len);
    }

    // Оператор сравнения векторов
    bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

// Функция для установки перспективной проекции
void customPerspective(float fovY, float aspect, float zNear, float zFar) {
    float fH = tan(fovY / 360.0f * M_PI) * zNear;  // Высота усеченной пирамиды
    float fW = fH * aspect;                       // Ширина усеченной пирамиды
    glFrustum(-fW, fW, -fH, fH, zNear, zFar);    // Установка перспективы
}

// Класс для создания геодезической сферы (икосаэдра с подразделением)
class GeodesicSphere {
private:
    std::vector<Vector3> vertices;  // Вершины многогранника
    std::vector<int> indices;       // Индексы вершин для треугольников
    float radius;                   // Радиус сферы
    int subdivision;                // Уровень подразделения

public:
    // Конструктор: создает сферу заданного радиуса с указанным уровнем подразделения
    GeodesicSphere(float r, int subdiv) : radius(r), subdivision(subdiv) {
        createIcosahedron();  // Создаем икосаэдр
        subdivide();          // Подразделяем его треугольники
    }
    
private:
    // Создание икосаэдра (20-гранника)
    void createIcosahedron() {
        // Золотое сечение
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

        // 12 вершин икосаэдра
        vertices = {
            Vector3(-1,  t,  0).normalize() * radius,  // Вершина 0
            Vector3(1,  t,  0).normalize() * radius,  // Вершина 1
            Vector3(-1, -t,  0).normalize() * radius,  // Вершина 2
            Vector3(1, -t,  0).normalize() * radius,  // Вершина 3
            Vector3(0, -1,  t).normalize() * radius,  // Вершина 4
            Vector3(0,  1,  t).normalize() * radius,  // Вершина 5
            Vector3(0, -1, -t).normalize() * radius,  // Вершина 6
            Vector3(0,  1, -t).normalize() * radius,  // Вершина 7
            Vector3(t,  0, -1).normalize() * radius,  // Вершина 8
            Vector3(t,  0,  1).normalize() * radius,  // Вершина 9
            Vector3(-t,  0, -1).normalize() * radius,  // Вершина 10
            Vector3(-t,  0,  1).normalize() * radius   // Вершина 11
        };

        // 20 треугольных граней икосаэдра (указаны индексы вершин)
        indices = {
            // 5 граней вокруг вершины 0
            0, 11, 5,   0, 5, 1,   0, 1, 7,   0, 7, 10,  0, 10, 11,
            // 5 граней вокруг вершины 1
            1, 5, 9,    5, 11, 4,  11, 10, 2, 10, 7, 6,   7, 1, 8,
            // 5 граней вокруг вершины 3
            3, 9, 4,    3, 4, 2,   3, 2, 6,   3, 6, 8,    3, 8, 9,
            // 5 промежуточных граней
            4, 9, 5,    2, 4, 11,  6, 2, 10,  8, 6, 7,    9, 8, 1
        };
    }
    
    // Метод для подразделения треугольников
    void subdivide() {
        for (int i = 0; i < subdivision; i++) {
            std::vector<int> newIndices;  // Новые индексы после подразделения
            std::map<std::pair<int, int>, int> midPointCache;  // Кэш для серединных точек

            // Обрабатываем каждый треугольник
            for (size_t j = 0; j < indices.size(); j += 3) {
                int v1 = indices[j];      // Первая вершина треугольника
                int v2 = indices[j + 1];  // Вторая вершина
                int v3 = indices[j + 2];  // Третья вершина

                // Находим или создаем середины каждого ребра
                int a = getMidPoint(v1, v2, midPointCache);
                int b = getMidPoint(v2, v3, midPointCache);
                int c = getMidPoint(v3, v1, midPointCache);

                // Добавляем 4 новых треугольника вместо исходного
                newIndices.insert(newIndices.end(), { v1, a, c });
                newIndices.insert(newIndices.end(), { v2, b, a });
                newIndices.insert(newIndices.end(), { v3, c, b });
                newIndices.insert(newIndices.end(), { a, b, c });
            }

            // Заменяем старые индексы новыми
            indices = newIndices;
        }
    }

    // Метод для нахождения или создания середины ребра
    int getMidPoint(int v1, int v2, std::map<std::pair<int, int>, int>& cache) {
        // Упорядочиваем вершины для уникального ключа
        if (v1 > v2) std::swap(v1, v2);
        auto key = std::make_pair(v1, v2);

        // Если середина уже вычислена - возвращаем ее
        if (cache.find(key) != cache.end()) {
            return cache[key];
        }

        // Вычисляем середину ребра
        Vector3 p1 = vertices[v1];
        Vector3 p2 = vertices[v2];
        Vector3 middle = (p1 + p2) * 0.5f;
        middle = middle.normalize() * radius;  // Проецируем на сферу

        // Добавляем новую вершину
        vertices.push_back(middle);
        cache[key] = vertices.size() - 1;  // Сохраняем в кэш
        return cache[key];
    }

public:
    // Геттеры для доступа к данным
    const std::vector<Vector3>& getVertices() const { return vertices; }
    const std::vector<int>& getIndices() const { return indices; }
    float getRadius() const { return radius; }
};

// Класс для представления двойственного многогранника
class DualPolyhedron {
private:
    std::vector<Vector3> dualVertices;           // Вершины двойственного многогранника
    std::vector<std::pair<int, int>> dualEdges;  // Ребра двойственного многогранника
    std::vector<std::vector<int>> faces;         // Грани (пяти- и шестиугольники)

public:
    // Конструктор создает двойственный многогранник из исходной сферы
    DualPolyhedron(const GeodesicSphere& sphere) {
        createDual(sphere);
    }

private:
    // Основной метод создания двойственного многогранника
    void createDual(const GeodesicSphere& sphere) {
        const auto& origVertices = sphere.getVertices();
        const auto& origIndices = sphere.getIndices();

        // Шаг 1: Вычисляем центроиды треугольников (вершины двойственного многогранника)
        for (size_t i = 0; i < origIndices.size(); i += 3) {
            Vector3 centroid(0, 0, 0);
            // Суммируем координаты вершин треугольника
            for (int j = 0; j < 3; j++) {
                const Vector3& v = origVertices[origIndices[i + j]];
                centroid = centroid + v;
            }
            // Делим на 3 для получения центра
            centroid = centroid / 3.0f;
            // Нормализуем и масштабируем до радиуса сферы
            dualVertices.push_back(centroid.normalize() * sphere.getRadius());
        }

        // Шаг 2: Находим смежные треугольники и создаем ребра
        std::map<std::pair<int, int>, std::vector<int>> edgeMap;

        // Для каждого треугольника...
        for (size_t i = 0; i < origIndices.size(); i += 3) {
            // Для каждого ребра треугольника...
            for (int j = 0; j < 3; j++) {
                int a = origIndices[i + j];
                int b = origIndices[i + (j + 1) % 3];
                // Упорядочиваем вершины ребра
                if (a > b) std::swap(a, b);
                // Добавляем треугольник в список для этого ребра
                edgeMap[{a, b}].push_back(i / 3);
            }
        }

        // Создаем ребра между центрами смежных треугольников
        for (const auto& entry : edgeMap) {
            if (entry.second.size() == 2) {
                dualEdges.emplace_back(entry.second[0], entry.second[1]);
            }
        }

        // Шаг 3: Находим грани (вокруг вершин исходного многогранника)
        std::vector<std::vector<int>> vertexToTriangles(origVertices.size());

        // Для каждой вершины исходного многогранника собираем все треугольники, которые ее содержат
        for (size_t i = 0; i < origIndices.size(); i++) {
            vertexToTriangles[origIndices[i]].push_back(i / 3);
        }

        // Для каждой вершины исходного многогранника...
        for (size_t vIdx = 0; vIdx < origVertices.size(); vIdx++) {
            const auto& triangles = vertexToTriangles[vIdx];
            if (triangles.empty()) continue;

            // Упорядочиваем треугольники вокруг вершины
            std::vector<int> ordered = orderTrianglesAroundVertex(origVertices[vIdx], triangles);
            faces.push_back(ordered);
        }
    }

    // Метод для упорядочивания треугольников вокруг вершины
    std::vector<int> orderTrianglesAroundVertex(const Vector3& center, const std::vector<int>& triangles) {
        if (triangles.empty()) return {};

        // Создаем систему координат для вычисления углов
        Vector3 normal = center.normalize();  // Нормаль к поверхности сферы
        Vector3 refDir = (dualVertices[triangles[0]] - center).normalize();  // Направление для отсчета углов
        Vector3 tangent = refDir.cross(normal).normalize();  // Касательное направление

        // Вычисляем углы для каждого треугольника
        std::vector<std::pair<float, int>> angleTrianglePairs;
        for (int t : triangles) {
            Vector3 dir = (dualVertices[t] - center).normalize();
            // Проецируем направление на плоскость
            float x = dir.dot(refDir);
            float y = dir.dot(tangent);
            // Вычисляем угол в плоскости
            float angle = atan2(y, x);
            angleTrianglePairs.emplace_back(angle, t);
        }

        // Сортируем треугольники по углу
        std::sort(angleTrianglePairs.begin(), angleTrianglePairs.end());

        // Формируем упорядоченный список треугольников
        std::vector<int> ordered;
        for (const auto& pair : angleTrianglePairs) {
            ordered.push_back(pair.second);
        }

        return ordered;
    }

public:
    // Метод для отрисовки двойственного многогранника
    void draw() const {
        // Рисуем грани
        for (const auto& face : faces) {
            // Выбираем цвет в зависимости от количества вершин
            if (face.size() == 5) {
                glColor3f(1.0f, 0.0f, 0.0f);  // Красный для пятиугольников
            }
            else {
                glColor3f(0.7f, 0.7f, 0.7f);  // Серый для шестиугольников
            }

            // Рисуем заполненную грань
            glBegin(GL_POLYGON);
            for (int vertexIndex : face) {
                const Vector3& v = dualVertices[vertexIndex];
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();

            // Рисуем контур грани
            glColor3f(0.0f, 0.0f, 0.0f);  // Черный для контуров
            glBegin(GL_LINE_LOOP);
            for (int vertexIndex : face) {
                const Vector3& v = dualVertices[vertexIndex];
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
        }
    }
};

// Глобальные переменные для управления камерой
float zoomLevel = 0.0f;    // Уровень приближения
float rotateX = 0.0f;      // Угол поворота по X
float rotateY = 0.0f;      // Угол поворота по Y
bool isRotating = false;   // Флаг вращения
double lastX, lastY;       // Последние координаты мыши

// Обратный вызов для колесика мыши (приближение/отдаление)
void scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
    zoomLevel += yOffset * 0.1f;
    // Ограничиваем диапазон приближения
    zoomLevel = std::max(-4.0f, std::min(4.0f, zoomLevel));
}

// Обратный вызов для кнопок мыши
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        isRotating = true;
        glfwGetCursorPos(window, &lastX, &lastY);  // Запоминаем позицию
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        isRotating = false;
    }
}

// Обратный вызов для движения мыши
void cursorPosCallback(GLFWwindow* window, double xPos, double yPos) {
    if (isRotating) {
        // Вычисляем смещение мыши
        double deltaX = xPos - lastX;
        double deltaY = yPos - lastY;
        lastX = xPos;
        lastY = yPos;

        // Обновляем углы поворота
        rotateY += deltaX * 0.5f;
        rotateX += deltaY * 0.5f;

        // Ограничиваем угол по X
        rotateX = std::max(-90.0f, std::min(90.0f, rotateX));
    }
}

int main() {
    // Инициализация GLFW
    if (!glfwInit()) {
        std::cerr << "Ошибка: Не удалось инициализировать GLFW" << std::endl;
        return -1;
    }

    // Создание окна
    GLFWwindow* window = glfwCreateWindow(800, 600, "Goldberg polyhedron", nullptr, nullptr);
    if (!window) {
        std::cerr << "Ошибка: Не удалось создать окно GLFW" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Сделать окно текущим контекстом
    glfwMakeContextCurrent(window);
    glEnable(GL_DEPTH_TEST);  // Включить тест глубины

    // Установка обратных вызовов
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);

    // Настройка проекции
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    customPerspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);
    glMatrixMode(GL_MODELVIEW);

    // Создание геодезической сферы и ее двойственного многогранника
    GeodesicSphere sphere(1.0f, 1);  // Радиус 1, 3 уровня подразделения
    DualPolyhedron dual(sphere);     // Создаем двойственный многогранник

    // Главный цикл приложения
    while (!glfwWindowShouldClose(window)) {
        // Очистка буферов
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();

        // Установка камеры
        glTranslatef(0.0f, 0.0f, -5.0f + zoomLevel);  // Положение камеры
        glRotatef(rotateX, 1.0f, 0.0f, 0.0f);         // Поворот по X
        glRotatef(rotateY, 0.0f, 1.0f, 0.0f);         // Поворот по Y

        // Отрисовка двойственного многогранника
        dual.draw();

        // Обмен буферов и обработка событий
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Очистка ресурсов
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}