#pragma once

#include <vector>

struct Vec2 {
    float x, y;
    Vec2(float x = 0, float y = 0) : x(x), y(y) {}
};

class HexGrid {
private:
    int width, height;
    float hexSize, hexWidth, hexHeight;
    std::vector<Vec2> vertices;
    std::vector<unsigned int> indices;
    std::vector<unsigned int> lineIndices;

    void generateGrid();

public:
    HexGrid(int width, int height, float hexSize);

    Vec2 getHexCenter(int col, int row);
    std::vector<Vec2> getHexVertices(const Vec2& center);
    std::pair<int, int> getHexAtPosition(float x, float y); // Добавленный метод

    const std::vector<Vec2>& getVertices() const { return vertices; }
    const std::vector<unsigned int>& getIndices() const { return indices; }
    const std::vector<unsigned int>& getLineIndices() const { return lineIndices; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }


};

