#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <GLFW/glfw3.h>
struct Vector3 {
    float x, y, z;

    Vector3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}

    Vector3 normalize() const {
        float len = std::sqrt(x * x + y * y + z * z);
        return Vector3(x / len, y / len, z / len);
    }

    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }
};

class GeodesicSphere {
private:
    std::vector<Vector3> vertices;
    std::vector<int> indices;
    float radius;
    int subdivision;

public:
    GeodesicSphere(float r, int subdiv) : radius(r), subdivision(subdiv) {
        createIcosahedron();
        subdivide();
    }

private:
    void createIcosahedron() {
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

        vertices = {
            Vector3(-1,  t,  0), Vector3(1, t,  0), Vector3(-1, -t,  0), Vector3(1, -t,  0),
            Vector3(0, -1,  t), Vector3(0, 1,  t), Vector3(0, -1, -t), Vector3(0,  1, -t),
            Vector3(t,  0, -1), Vector3(t, 0,  1), Vector3(-t,  0, -1), Vector3(-t,  0,  1)
        };

        for (auto& v : vertices) {
            v = v.normalize() * radius;
        }

        indices = {
            0, 11, 5,    0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
            1, 5, 9,     5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
            3, 9, 4,     3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
            4, 9, 5,     2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
        };
    }

    void subdivide() {
        for (int i = 0; i < subdivision; i++) {
            std::vector<int> newIndices;
            std::map<std::pair<int, int>, int> midPointCache;

            for (size_t j = 0; j < indices.size(); j += 3) {
                int v1 = indices[j];
                int v2 = indices[j + 1];
                int v3 = indices[j + 2];

                int a = getMidPoint(v1, v2, midPointCache);
                int b = getMidPoint(v2, v3, midPointCache);
                int c = getMidPoint(v3, v1, midPointCache);

                newIndices.push_back(v1); newIndices.push_back(a); newIndices.push_back(c);
                newIndices.push_back(v2); newIndices.push_back(b); newIndices.push_back(a);
                newIndices.push_back(v3); newIndices.push_back(c); newIndices.push_back(b);
                newIndices.push_back(a); newIndices.push_back(b); newIndices.push_back(c);
            }

            indices = newIndices;
        }
    }

    int getMidPoint(int v1, int v2, std::map<std::pair<int, int>, int>& cache) {
        bool reverse = v1 > v2;
        if (reverse) std::swap(v1, v2);

        auto key = std::make_pair(v1, v2);
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }

        Vector3 p1 = vertices[v1];
        Vector3 p2 = vertices[v2];
        Vector3 middle((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f, (p1.z + p2.z) * 0.5f);

        middle = middle.normalize() * radius;

        vertices.push_back(middle);
        cache[key] = vertices.size() - 1;

        return vertices.size() - 1;
    }

public:
    void draw() const {
        // Рисуем закрашенные треугольники
        glColor3f(0.0f, 0.0f, 0.0f); // Черный цвет для заливки
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < indices.size(); i += 3) {
            Vector3 v1 = vertices[indices[i]];
            Vector3 v2 = vertices[indices[i + 1]];
            Vector3 v3 = vertices[indices[i + 2]];

            glVertex3f(v1.x, v1.y, v1.z);
            glVertex3f(v2.x, v2.y, v2.z);
            glVertex3f(v3.x, v3.y, v3.z);
        }
        glEnd();

        // Рисуем обводку треугольников
        glColor3f(1.0f, 1.0f, 1.0f); // Белый цвет для обводки
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i < indices.size(); i += 3) {
            Vector3 v1 = vertices[indices[i]];
            Vector3 v2 = vertices[indices[i + 1]];
            Vector3 v3 = vertices[indices[i + 2]];

            glVertex3f(v1.x, v1.y, v1.z);
            glVertex3f(v2.x, v2.y, v2.z);
            glVertex3f(v3.x, v3.y, v3.z);
        }
        glEnd();
    }

    const std::vector<Vector3>& getVertices() const { return vertices; }
    const std::vector<int>& getIndices() const { return indices; }
};

GeodesicSphere sphere(1.0f, 3);

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    sphere.draw();
    glfwSwapBuffers(glfwGetCurrentContext());
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(800, 600, "Geodesic Sphere", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        display();
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
