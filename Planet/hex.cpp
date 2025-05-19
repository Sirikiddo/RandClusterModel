#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <iostream>


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <vector>
#include <cmath>
#include <queue>
#include <map>
#include <set>

#include "hex.h"


// Реализация HexGrid

HexGrid::HexGrid(int width, int height, float hexSize, float screenWidth, float screenHeight)
    : width(width), height(height), hexSize(hexSize) {
    hexWidth = 2 * hexSize;
    hexHeight = static_cast<float>(std::sqrt(3.0)) * hexSize;

    float gridCenterX = (0 + hexSize * 1.5f * (width - 1)) / 2.0f;
    float gridCenterY = (0 + std::sqrt(3.0f) * (height - 1 + 0.5f)) / 2.0f * hexSize;

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
    std::vector<Vec2> verts;
    verts.reserve(6);
    for (int i = 0; i < 6; ++i) {
        float angle = static_cast<float>(M_PI) / 6.0f + i * static_cast<float>(M_PI) / 3.0f + static_cast<float>(M_PI) / 2.0f;
        verts.emplace_back(
            center.x + hexSize * std::cos(angle),
            center.y + hexSize * std::sin(angle)
        );
    }
    return verts;
}

std::pair<int, int> HexGrid::getHexAtPosition(float x, float y) {
    int approxCol = static_cast<int>((x - offset.x) / (hexSize * 1.5f));
    int approxRow = static_cast<int>((y - offset.y) / hexHeight - 0.5f * (approxCol % 2));

    std::cout << "Input coords: (" << x << ", " << y << ")\n";
    std::cout << "Approx hex: (" << approxCol << ", " << approxRow << ")\n";

    std::vector<std::pair<int, int>> candidates;
    candidates.emplace_back(approxCol, approxRow);
    int dx[6] = { -1, 0, 1, 1, 0, -1 };
    int dy[6] = { 0, -1, 0, 1, 1,  1 };
    for (int i = 0; i < 6; ++i) {
        candidates.emplace_back(approxCol + dx[i], approxRow + dy[i]);
    }

    for (const auto& candidate : candidates) {
        int col = candidate.first, row = candidate.second;
        if (col < 0 || col >= width || row < 0 || row >= height) continue;

        Vec2 center = getHexCenter(col, row);
        auto hexVerts = getHexVertices(center);
        bool inside = false;
        for (size_t i = 0, j = hexVerts.size() - 1; i < hexVerts.size(); j = i++) {
            if (((hexVerts[i].y > y) != (hexVerts[j].y > y)) &&
                (x < (hexVerts[j].x - hexVerts[i].x) * (y - hexVerts[i].y) /
                    (hexVerts[j].y - hexVerts[i].y) +
                    hexVerts[i].x)) {
                inside = !inside;
            }
        }
        if (inside) return std::make_pair(col, row);
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

            int base = static_cast<int>(vertices.size());
            for (const auto& v : hexVerts) {
                vertices.push_back(v);
            }

            for (int i = 0; i < 6; ++i) {
                indices.push_back(base);
                indices.push_back(base + i);
                indices.push_back(base + (i + 1) % 6);

                lineIndices.push_back(base + i);
                lineIndices.push_back(base + (i + 1) % 6);
            }
        }
    }

    // Отладочный вывод
    std::cout << "Generated " << vertices.size() << " vertices, "
        << indices.size() << " indices, "
        << lineIndices.size() << " line indices.\n";
}

std::vector<std::pair<int, int>> HexGrid::findPath(
    const std::pair<int, int>& start,
    const std::pair<int, int>& end,
    const std::map<std::pair<int, int>, int>& hexClickCount
) {
    if (start == end) return { start };

    struct Node {
        int col, row;
        float g, h, f;
        Node* parent;
        Node(int c, int r, Node* p = nullptr) : col(c), row(r), g(0), h(0), f(0), parent(p) {}
        bool operator<(const Node& o) const { return f > o.f; }
    };

    std::priority_queue<Node> open;
    std::set<std::pair<int, int>> closed;
    std::map<std::pair<int, int>, Node*> nodes;

    Node* startNode = new Node(start.first, start.second);
    startNode->h = std::abs(start.first - end.first) + std::abs(start.second - end.second);
    startNode->f = startNode->h;
    open.push(*startNode);
    nodes[start] = startNode;

    while (!open.empty()) {
        Node current = open.top();
        open.pop();

        if (current.col == end.first && current.row == end.second) {
            std::vector<std::pair<int, int>> path;
            Node* n = nodes[{current.col, current.row}];
            while (n) {
                path.emplace_back(n->col, n->row);
                n = n->parent;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        closed.insert({ current.col, current.row });

        auto neighbors = getNeighbors(current.col, current.row, hexClickCount);
        for (const auto& nb : neighbors) {
            if (closed.count(nb)) continue;

            float newG = current.g + 1.0f;
            Node* neighbor = nodes.count(nb) ? nodes[nb] : nullptr;

            if (!neighbor || newG < neighbor->g) {
                if (!neighbor) {
                    neighbor = new Node(nb.first, nb.second);
                    nodes[nb] = neighbor;
                }
                neighbor->parent = nodes[{current.col, current.row}];
                neighbor->g = newG;
                neighbor->h = std::abs(nb.first - end.first) + std::abs(nb.second - end.second);
                neighbor->f = neighbor->g + neighbor->h;
                open.push(*neighbor);
            }
        }
    }
    return {};
}

std::vector<std::pair<int, int>> HexGrid::getNeighbors(
    int col,
    int row,
    const std::map<std::pair<int, int>, int>& hexClickCount
) {
    std::vector<std::pair<int, int>> res;
    int parity = col % 2;
    int dirs[6][2] = {
        {1, 0}, {1 - parity, -1 + parity}, {0, -1},
        {-1, 0}, {-1 + parity, -1 + parity}, {0, 1}
    };
    for (auto& d : dirs) {
        int nCol = col + d[0], nRow = row + d[1];
        if (nCol >= 0 && nCol < width && nRow >= 0 && nRow < height) {
            if (!(hexClickCount.count({ nCol,nRow }) && hexClickCount.at({ nCol,nRow }) == 1)) {
                res.emplace_back(nCol, nRow);
            }
        }
    }
    return res;
}
