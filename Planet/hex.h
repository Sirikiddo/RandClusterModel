// hex.h (изменения минимальны, только добавил метод для получения hexHeight)
#pragma once

#include <vector>
#include <unordered_map>
#include <queue>
#include <set>
#include <map> 
#include <functional>

namespace std {
    template <>
    struct hash<std::pair<int, int>> {
        size_t operator()(const std::pair<int, int>& p) const {
            return hash<int>()(p.first) ^ (hash<int>()(p.second) << 1);
        }
    };
}

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
    Vec2 offset;

    void generateGrid();

public:
    HexGrid(int width, int height, float hexSize, float screenWidth, float screenHeight);

    Vec2 getHexCenter(int col, int row);
    std::vector<Vec2> getHexVertices(const Vec2& center);
    std::pair<int, int> getHexAtPosition(float x, float y);

    const std::vector<Vec2>& getVertices() const { return vertices; }
    const std::vector<unsigned int>& getIndices() const { return indices; }
    const std::vector<unsigned int>& getLineIndices() const { return lineIndices; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    float getHexHeight() const { return hexHeight; } // Добавлен новый метод

    std::vector<std::pair<int, int>> findPath(
        const std::pair<int, int>& start,
        const std::pair<int, int>& end,
        const std::map<std::pair<int, int>, int>& hexClickCount
    );

    std::vector<std::pair<int, int>> getNeighbors(
        int col,
        int row,
        const std::map<std::pair<int, int>, int>& hexClickCount
    );

    float getHexSize() const { return hexSize; }
};