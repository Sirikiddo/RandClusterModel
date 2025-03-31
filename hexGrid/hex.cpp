#include "hex.h"
#include <vector>
#include <cmath>
#include <glew.h>
#include "corecrt_math_defines.h"

HexGrid::HexGrid(int width, int height, float hexSize, float screenWidth, float screenHeight)
    : width(width), height(height), hexSize(hexSize) {
    hexWidth = 2 * hexSize;
    hexHeight = sqrt(3) * hexSize;

    // Вычисляем размеры сетки по центрам
    // Минимальные координаты считаются равными нулю,
    // а максимальные по X: hexSize * 1.5 * (width - 1)
    // по Y: для столбцов сдвигаем на hexHeight*0.5, поэтому максимальное значение = hexHeight * (height - 1 + 0.5)
    float gridCenterX = (0 + hexSize * 1.5f * (width - 1)) / 2.0f;
    float gridCenterY = (0 + hexHeight * (height - 1 + 0.5f)) / 2.0f;

    // Вычисляем смещение, чтобы центр сетки совпадал с центром окна
    offset.x = screenWidth / 2.0f - gridCenterX;
    offset.y = screenHeight / 2.0f - gridCenterY;

    generateGrid();
}

Vec2 HexGrid::getHexCenter(int col, int row) {
    float x = hexSize * 1.5f * col;
    float y = hexHeight * (row + 0.5f * (col % 2));
    return Vec2(x, y) + offset;
}

std::vector<Vec2> HexGrid::getHexVertices(const Vec2& center) {
    std::vector<Vec2> vertices;
    for (int i = 0; i < 6; ++i) {
        float angle = M_PI / 6.0f + i * M_PI / 3.0f + M_PI / 2.0f;
        vertices.emplace_back(
            center.x + hexSize * cos(angle),
            center.y + hexSize * sin(angle)
        );
    }
    return vertices;
}

std::pair<int, int> HexGrid::getHexAtPosition(float x, float y) {
    // Определяем приблизительный столбец и строку
    int approxCol = static_cast<int>((x - offset.x) / (hexSize * 1.5f));
    int approxRow = static_cast<int>((y - offset.y) / hexHeight - 0.5f * (approxCol % 2));

    // Собираем кандидатов: первоначально выбранный и его соседи
    std::vector<std::pair<int, int>> candidates;
    candidates.push_back({ approxCol, approxRow });
    int dx[6] = { -1, 0, 1, 1, 0, -1 };
    int dy[6] = { 0, -1, 0, 1, 1, 1 };
    for (int i = 0; i < 6; i++) {
        int ncol = approxCol + dx[i];
        int nrow = approxRow + dy[i];
        candidates.push_back({ ncol, nrow });
    }

    // Проверяем кандидатов с помощью алгоритма определения попадания в многоугольник
    for (const auto& candidate : candidates) {
        int col = candidate.first;
        int row = candidate.second;
        if (col < 0 || col >= width || row < 0 || row >= height)
            continue;

        Vec2 center = getHexCenter(col, row);
        std::vector<Vec2> hexVerts = getHexVertices(center);
        bool inside = false;
        for (size_t i = 0, j = hexVerts.size() - 1; i < hexVerts.size(); j = i++) {
            if (((hexVerts[i].y > y) != (hexVerts[j].y > y)) &&
                (x < (hexVerts[j].x - hexVerts[i].x) * (y - hexVerts[i].y) / (hexVerts[j].y - hexVerts[i].y) + hexVerts[i].x)) {
                inside = !inside;
            }
        }
        if (inside)
            return std::make_pair(col, row);
    }

    return std::make_pair(-1, -1);
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
