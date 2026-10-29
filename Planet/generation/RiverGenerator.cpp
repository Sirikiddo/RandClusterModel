#include "generation/RiverGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>

namespace {

    // Подъём воды стоит дорого, спуск — немного дешевле: река течёт вниз.
    constexpr float kClimbPenalty = 0.70f;
    constexpr float kDescentDiscount = 0.12f;
    constexpr float kMaxDescentDiscount = 0.45f;

    float angularDistance(const QVector3D& a, const QVector3D& b) {
        const float dot = std::clamp(
            QVector3D::dotProduct(a.normalized(), b.normalized()), -1.0f, 1.0f);
        return std::acos(dot);
    }

    // Насколько "легко" реке идти через биом. Песок и камень пропускают воду хуже
    // луга и джунглей, поэтому русло стремится в мягкие низины.
    float riverTerrainFactor(Biome biome) {
        switch (biome) {
        case Biome::Sea:     return 1.00f;
        case Biome::Grass:   return 1.00f;
        case Biome::Rock:    return 1.35f;
        case Biome::Snow:    return 1.55f;
        case Biome::Tundra:  return 1.20f;
        case Biome::Desert:  return 1.30f;
        case Biome::Savanna: return 1.10f;
        case Biome::Jungle:  return 1.15f;
        }
        return 1.20f;
    }

    float riverStepCost(const Cell& from, const Cell& to) {
        const float distance = angularDistance(from.centroid, to.centroid);
        const float terrain =
            0.5f * (riverTerrainFactor(from.biome) + riverTerrainFactor(to.biome));

        const int climb = to.height - from.height;
        float slope = 1.0f;
        if (climb > 0) {
            slope = 1.0f + static_cast<float>(climb) * kClimbPenalty;
        }
        else if (climb < 0) {
            slope = 1.0f - std::min(
                kMaxDescentDiscount, static_cast<float>(-climb) * kDescentDiscount);
        }
        return distance * terrain * slope;
    }

    // Морская клетка у берега: у неё есть хотя бы один сосед-суша. Только такие
    // клетки могут быть устьем реки.
    bool isCoastalSeaCell(const HexSphereModel& model, int cellId) {
        const auto& cells = model.cells();
        if (cellId < 0 || cellId >= static_cast<int>(cells.size())) {
            return false;
        }
        if (cells[static_cast<size_t>(cellId)].biome != Biome::Sea) {
            return false;
        }
        for (int neighbor : cells[static_cast<size_t>(cellId)].neighbors) {
            if (neighbor >= 0 && neighbor < static_cast<int>(cells.size())
                && cells[static_cast<size_t>(neighbor)].biome != Biome::Sea) {
                return true;
            }
        }
        return false;
    }

    // Dijkstra от устья: ищем ближайшее другое устье, идя только по суше.
    // Море нельзя проходить насквозь — оно может быть только финишем.
    RiverPath findRiverFrom(
        const HexSphereModel& model,
        int mouth,
        const std::vector<char>& blocked,
        const RiverGenerationOptions& options) {

        const auto& cells = model.cells();
        const int count = static_cast<int>(cells.size());
        constexpr float kInfinity = std::numeric_limits<float>::infinity();

        std::vector<float> distance(static_cast<size_t>(count), kInfinity);
        std::vector<int> parent(static_cast<size_t>(count), -1);
        // Сколько сухопутных клеток пройдено от устья: по нему отсеиваем слишком
        // короткие «реки-перемычки» и не прекращаем поиск из-за них.
        std::vector<int> landDepth(static_cast<size_t>(count), 0);
        std::vector<char> closed(static_cast<size_t>(count), 0);

        struct Node {
            float distance;
            int cellId;
        };
        struct Compare {
            bool operator()(const Node& a, const Node& b) const { return a.distance > b.distance; }
        };
        std::priority_queue<Node, std::vector<Node>, Compare> queue;

        distance[static_cast<size_t>(mouth)] = 0.0f;
        queue.push({ 0.0f, mouth });

        int goal = -1;
        float goalDistance = kInfinity;
        int visited = 0;

        while (!queue.empty()) {
            const Node node = queue.top();
            queue.pop();

            const int current = node.cellId;
            if (closed[static_cast<size_t>(current)]) {
                continue;
            }
            if (node.distance > goalDistance) {
                break;
            }
            closed[static_cast<size_t>(current)] = 1;
            if (++visited > options.searchNodeLimit) {
                break;
            }

            for (int neighbor : cells[static_cast<size_t>(current)].neighbors) {
                if (neighbor < 0 || neighbor >= count
                    || closed[static_cast<size_t>(neighbor)]
                    || blocked[static_cast<size_t>(neighbor)]) {
                    continue;
                }

                if (cells[static_cast<size_t>(neighbor)].biome == Biome::Sea) {
                    // Морская клетка может быть только финишем.
                    if (current == mouth || !isCoastalSeaCell(model, neighbor)) {
                        continue;
                    }
                    // Слишком короткая река (озеро-перемычка) — не финиш: ищем дальше.
                    const int depth = landDepth[static_cast<size_t>(current)];
                    if (depth < options.minLandCells
                        || depth + 2 > options.maxRiverCells) {
                        continue;
                    }
                    const float candidate = distance[static_cast<size_t>(current)]
                        + riverStepCost(cells[static_cast<size_t>(current)],
                            cells[static_cast<size_t>(neighbor)]);
                    if (candidate < goalDistance) {
                        goalDistance = candidate;
                        goal = neighbor;
                        parent[static_cast<size_t>(neighbor)] = current;
                    }
                    continue;
                }

                const float candidate = distance[static_cast<size_t>(current)]
                    + riverStepCost(cells[static_cast<size_t>(current)],
                        cells[static_cast<size_t>(neighbor)]);
                if (candidate < distance[static_cast<size_t>(neighbor)]) {
                    distance[static_cast<size_t>(neighbor)] = candidate;
                    parent[static_cast<size_t>(neighbor)] = current;
                    landDepth[static_cast<size_t>(neighbor)] =
                        landDepth[static_cast<size_t>(current)] + 1;
                    queue.push({ candidate, neighbor });
                }
            }
        }

        if (goal < 0) {
            return {};
        }

        std::vector<int> path;
        for (int current = goal; current != -1; current = parent[static_cast<size_t>(current)]) {
            path.push_back(current);
            if (current == mouth) {
                break;
            }
        }
        std::reverse(path.begin(), path.end());

        if (path.size() < 3 || path.front() != mouth) {
            return {};
        }
        const int landCells = static_cast<int>(path.size()) - 2;
        if (landCells < options.minLandCells
            || static_cast<int>(path.size()) > options.maxRiverCells) {
            return {};
        }

        RiverPath river;
        river.cellIds = std::move(path);
        return river;
    }

} // namespace

std::vector<RiverPath> RiverGenerator::generate(
    const HexSphereModel& model,
    const RiverGenerationOptions& options) {

    std::vector<RiverPath> rivers;

    const auto& cells = model.cells();
    const int count = static_cast<int>(cells.size());
    if (count <= 0 || options.maxRivers <= 0 || options.maxRiverCells < 3) {
        return rivers;
    }

    std::vector<int> mouths;
    mouths.reserve(static_cast<size_t>(count));
    for (int cellId = 0; cellId < count; ++cellId) {
        if (isCoastalSeaCell(model, cellId)) {
            mouths.push_back(cellId);
        }
    }
    if (mouths.size() < 2) {
        return rivers;
    }

    // Порядок перебора устьев детерминирован и зависит только от seed.
    std::mt19937 rng(options.seed);
    std::shuffle(mouths.begin(), mouths.end(), rng);

    std::vector<char> blocked(static_cast<size_t>(count), 0);

    for (int mouth : mouths) {
        if (static_cast<int>(rivers.size()) >= options.maxRivers) {
            break;
        }
        if (blocked[static_cast<size_t>(mouth)]) {
            continue;
        }

        RiverPath river = findRiverFrom(model, mouth, blocked, options);
        if (!river.isValid()) {
            continue;
        }

        // Реки не пересекаются: занятые клетки становятся препятствием для следующих.
        for (int cellId : river.cellIds) {
            blocked[static_cast<size_t>(cellId)] = 1;
        }
        rivers.push_back(std::move(river));
    }

    return rivers;
}
