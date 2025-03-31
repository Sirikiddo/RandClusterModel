#pragma once

#include <vector>

struct Vec2 {
    float x, y;
    Vec2(float x = 0, float y = 0) : x(x), y(y) {}
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }
};

class HexGrid {
private:
    int width, height;
    float hexSize, hexWidth, hexHeight;
    std::vector<Vec2> vertices;
    std::vector<unsigned int> indices;
    std::vector<unsigned int> lineIndices;
    Vec2 offset; // —двиг дл€ центрировани€

    void generateGrid();

public:
    // Ќовый конструктор с дополнительными параметрами дл€ размеров окна
    HexGrid(int width, int height, float hexSize, float screenWidth, float screenHeight);

    // “еперь метод возвращает центр с учетом смещени€
    Vec2 getHexCenter(int col, int row);
    std::vector<Vec2> getHexVertices(const Vec2& center);
    std::pair<int, int> getHexAtPosition(float x, float y); // ћетод дл€ определени€, в какой шестиугольник попали

    const std::vector<Vec2>& getVertices() const { return vertices; }
    const std::vector<unsigned int>& getIndices() const { return indices; }
    const std::vector<unsigned int>& getLineIndices() const { return lineIndices; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }
};
