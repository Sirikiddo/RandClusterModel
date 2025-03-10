#include "hex.h"
#include <vector>
#include <cmath>
#include <glew.h>
#include "corecrt_math_defines.h"

HexGrid::HexGrid(int width, int height, float hexSize)
    : width(width), height(height), hexSize(hexSize) {
    hexWidth = hexSize * 2;
    hexHeight = sqrt(2) * hexSize;
    generateGrid();
}


Vec2 HexGrid::getHexCenter(int col, int row) {
    float x = hexWidth * (col + 0.5f * (row % 2)); //* 0.75 ;
    float y = row * hexHeight; //* 0.75f;
    return Vec2(x, y);
}



std::vector<Vec2> HexGrid::getHexVertices(const Vec2& center) {
    std::vector<Vec2> vertices;
    for (int i = 0; i < 6; ++i) {
        float angle = i * M_PI / 3.0f;
        vertices.emplace_back(
            center.x + hexSize * cos(angle),
            center.y + hexSize * sin(angle)
        );
    }
    return vertices;
}


std::pair<int, int> HexGrid::getHexAtPosition(float x, float y) {
    float hexHeight = sqrt(3) * hexSize;
    float hexWidth = 2 * hexSize;

    float gridY = y / (hexHeight * 0.75f);
    int row = static_cast<int>(std::round(gridY));
    if (row < 0 || row >= height) return { -1, -1 };

    float offset = (row % 2) * 0.5f;
    float gridX = (x / (hexWidth * 0.75f)) - offset;
    int col = static_cast<int>(std::round(gridX));
    if (col < 0 || col >= width) return { -1, -1 };

    bool inside = false;
    int n = vertices.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& p1 = vertices[j];
        const Vec2& p2 = vertices[i];
        if (p1.y == p2.y) continue;

        if ((y > std::min(p1.y, p2.y)) && (y <= std::max(p1.y, p2.y))) {
            float t = (y - p1.y) / (p2.y - p1.y);
            float intersectX = p1.x + t * (p2.x - p1.x);
            if (x <= intersectX) {
                inside = !inside;
            }
        }
    }

    return inside ? std::make_pair(col, row) : std::make_pair(-1, -1);
}

void HexGrid::generateGrid() {
    vertices.clear();
    indices.clear();
    lineIndices.clear();

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            Vec2 center = getHexCenter(col, row);
            auto hexVerts = getHexVertices(center);

            int base = vertices.size();
            for (const auto& vert : hexVerts) {
                vertices.push_back(vert);
            }

            for (int i = 0; i < 6; ++i) {
                indices.push_back(base);
                indices.push_back(base + i);
                indices.push_back(base + ((i + 1) % 6));
            }

            for (int i = 0; i < 6; ++i) {
                lineIndices.push_back(base + i);
                lineIndices.push_back(base + ((i + 1) % 6));
            }
        }
    }
}
