#include "hex.h"
#include <vector>
#include <cmath>
#include <glew.h>
#include "corecrt_math_defines.h"
#include <queue>
#include <map>

HexGrid::HexGrid(int width, int height, float hexSize, float screenWidth, float screenHeight)
    : width(width), height(height), hexSize(hexSize) {
    hexWidth = 2 * hexSize;
    hexHeight = static_cast<float>(sqrt(3)) * hexSize;

    float gridCenterX = (0 + hexSize * 1.5f * (width - 1)) / 2.0f;
    float gridCenterY = (0 + hexHeight * (height - 1 + 0.5f)) / 2.0f;

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
    // ?????????? ??????????????? ??????? ? ??????
    int approxCol = static_cast<int>((x - offset.x) / (hexSize * 1.5f));
    int approxRow = static_cast<int>((y - offset.y) / hexHeight - 0.5f * (approxCol % 2));

    // ???????? ??????????: ????????????? ????????? ? ??? ??????
    std::vector<std::pair<int, int>> candidates;
    candidates.push_back({ approxCol, approxRow });
    int dx[6] = { -1, 0, 1, 1, 0, -1 };
    int dy[6] = { 0, -1, 0, 1, 1, 1 };
    for (int i = 0; i < 6; i++) {
        int ncol = approxCol + dx[i];
        int nrow = approxRow + dy[i];
        candidates.push_back({ ncol, nrow });
    }

    // ????????? ?????????? ? ??????? ????????? ??????????? ????????? ? ?????????????
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

struct Node {
    int col, row;
    float g, h, f;
    Node* parent;

    Node(int c, int r, Node* p = nullptr)
        : col(c), row(r), g(0), h(0), f(0), parent(p) {
    }

    bool operator<(const Node& other) const {
        return f > other.f;
    }
};

std::vector<std::pair<int, int>> HexGrid::findPath(
    const std::pair<int, int>& start,
    const std::pair<int, int>& end,
    const std::map<std::pair<int, int>, int>& hexClickCount
) {
    int startCol = start.first;
    int startRow = start.second;
    int endCol = end.first;
    int endRow = end.second;

    std::priority_queue<Node> open;
    std::set<std::pair<int, int>> closed;
    std::unordered_map<std::pair<int, int>, Node*> nodes;

    Node* startNode = new Node(startCol, startRow);
    startNode->h = abs(startCol - endCol) + abs(startRow - endRow);
    startNode->f = startNode->h;
    open.push(*startNode);
    nodes[{startCol, startRow}] = startNode;

    while (!open.empty()) {
        Node current = open.top();
        open.pop();

        if (current.col == endCol && current.row == endRow) {
            std::vector<std::pair<int, int>> path;
            Node* node = &current;
            while (node) {
                path.push_back({ node->col, node->row });
                node = node->parent;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        closed.insert({ current.col, current.row });

        auto neighbors = getNeighbors(current.col, current.row, hexClickCount);
        for (const auto& neighborCoord : neighbors) {
            int nCol = neighborCoord.first;
            int nRow = neighborCoord.second;

            if (closed.count({ nCol, nRow })) continue;

            float newG = current.g + 1;
            Node* neighbor = nodes[{nCol, nRow}];
            if (!neighbor || newG < neighbor->g) {
                if (!neighbor) {
                    neighbor = new Node(nCol, nRow);
                    nodes[{nCol, nRow}] = neighbor;
                }
                neighbor->parent = new Node(current.col, current.row, current.parent);
                neighbor->g = newG;
                neighbor->h = abs(nCol - endCol) + abs(nRow - endRow);
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
    std::vector<std::pair<int, int>> neighbors;
    int parity = col % 2;
    int dirs[6][2] = {
        {1, 0}, {1 - parity, -1 + parity}, {0, -1},
        {-1, 0}, {-1 + parity, -1 + parity}, {0, 1}
    };

    for (const auto& dir : dirs) {
        int dx = dir[0];
        int dy = dir[1];
        int nCol = col + dx;
        int nRow = row + dy;

        if (nCol >= 0 && nCol < this->width && nRow >= 0 && nRow < this->height) {
            if (hexClickCount.count({ nCol, nRow }) && hexClickCount.at({ nCol, nRow }) == 1) {
                continue;
            }
            neighbors.emplace_back(nCol, nRow);
        }
    }
    return neighbors;
}