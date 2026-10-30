#include "generation/RiverGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>

namespace {

    // Спуск дешевле подъёма: река должна стекать с горы вниз, а не искать
    // обходные долины с подъёмами. Штраф за подъём заметно выше старого,
    // потому что теперь исток фиксирован в горах.
    constexpr float kClimbPenalty = 2.40f;
    constexpr float kDescentDiscount = 0.12f;
    constexpr float kMaxDescentDiscount = 0.42f;

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

    int heightQuantile(std::vector<int>& heights, float quantile) {
        if (heights.empty()) {
            return 0;
        }
        const float q = std::clamp(quantile, 0.0f, 1.0f);
        const size_t index = std::min(
            heights.size() - 1,
            static_cast<size_t>(q * static_cast<float>(heights.size() - 1)));
        std::nth_element(heights.begin(), heights.begin() + static_cast<ptrdiff_t>(index), heights.end());
        return heights[index];
    }

    // Исток должен лежать на склоне: рядом есть клетка ниже, иначе вода
    // "начинается" на плато и река выглядит случайной.
    bool hasLowerLandNeighbor(const HexSphereModel& model, int cellId) {
        const auto& cells = model.cells();
        const Cell& cell = cells[static_cast<size_t>(cellId)];
        for (int neighbor : cell.neighbors) {
            if (neighbor < 0 || neighbor >= static_cast<int>(cells.size())) {
                continue;
            }
            const Cell& other = cells[static_cast<size_t>(neighbor)];
            if (other.biome != Biome::Sea && other.height < cell.height) {
                return true;
            }
        }
        return false;
    }

    struct SourceCandidate {
        int cellId = -1;
        int height = 0;
        int score = 0;
        QVector3D direction;
    };

    std::vector<SourceCandidate> collectMountainSources(
        const HexSphereModel& model,
        const RiverGenerationOptions& options) {
        const auto& cells = model.cells();
        const int count = static_cast<int>(cells.size());

        std::vector<int> landHeights;
        landHeights.reserve(static_cast<size_t>(count));
        for (const Cell& cell : cells) {
            if (cell.biome != Biome::Sea) {
                landHeights.push_back(cell.height);
            }
        }
        if (landHeights.empty()) {
            return {};
        }

        // Истоки — верхняя доля суши. Снег на полюсах сам по себе не гора,
        // поэтому для снежных/каменных клеток дополнительно проверяется высота.
        const int highThreshold = std::max(1, heightQuantile(landHeights, options.sourceHeightQuantile));
        const int topThreshold = std::max(highThreshold, heightQuantile(landHeights, 0.95f));

        std::vector<SourceCandidate> candidates;
        candidates.reserve(static_cast<size_t>(count));

        for (int cellId = 0; cellId < count; ++cellId) {
            const Cell& cell = cells[static_cast<size_t>(cellId)];
            if (cell.biome == Biome::Sea || cell.height < highThreshold) {
                continue;
            }
            const bool mountainBiome = cell.biome == Biome::Rock || cell.biome == Biome::Snow;
            if (!mountainBiome && cell.height < topThreshold) {
                continue;
            }
            if (!hasLowerLandNeighbor(model, cellId)) {
                continue;
            }
            candidates.push_back({
                cellId, cell.height, mountainBiome ? 8 : 0, cell.centroid.normalized() });
        }

        // На случай планеты без выраженных гор: всё равно стартуем с самых
        // высоких клеток, а не с произвольных низин.
        if (candidates.size() < static_cast<size_t>(std::max(options.maxRivers, 1))) {
            for (int cellId = 0; cellId < count; ++cellId) {
                const Cell& cell = cells[static_cast<size_t>(cellId)];
                if (cell.biome == Biome::Sea || cell.height < topThreshold) {
                    continue;
                }
                if (!hasLowerLandNeighbor(model, cellId)) {
                    continue;
                }
                const bool already = std::any_of(
                    candidates.begin(), candidates.end(),
                    [cellId](const SourceCandidate& value) { return value.cellId == cellId; });
                if (!already) {
                    candidates.push_back({ cellId, cell.height, 0, cell.centroid.normalized() });
                }
            }
        }

        // Детерминированный разброс: высокие горы сохраняют приоритет, но seed
        // выбирает, какие из близких вершин станут истоками.
        std::mt19937 rng(options.seed);
        std::uniform_int_distribution<int> jitter(-2, 2);
        for (SourceCandidate& candidate : candidates) {
            candidate.score += candidate.height * 4 + jitter(rng);
        }
        std::stable_sort(candidates.begin(), candidates.end(),
            [](const SourceCandidate& a, const SourceCandidate& b) {
                return a.score > b.score;
            });

        std::vector<SourceCandidate> spaced;
        spaced.reserve(candidates.size());
        for (const SourceCandidate& candidate : candidates) {
            const bool tooClose = std::any_of(
                spaced.begin(), spaced.end(),
                [&](const SourceCandidate& other) {
                    return angularDistance(candidate.direction, other.direction)
                        < options.sourceMinSeparation;
                });
            if (!tooClose) {
                spaced.push_back(candidate);
            }
        }

        if (spaced.size() < 2 && candidates.size() >= 2) {
            return candidates;
        }
        return spaced;
    }

    // Dijkstra от горного истока к ближайшему морскому устью. По суше идём с
    // сильным штрафом за подъём, море может быть только финишем.
    RiverPath findRiverFromSource(
        const HexSphereModel& model,
        int source,
        const std::vector<char>& blocked,
        const RiverGenerationOptions& options) {

        const auto& cells = model.cells();
        const int count = static_cast<int>(cells.size());
        constexpr float kInfinity = std::numeric_limits<float>::infinity();

        std::vector<float> distance(static_cast<size_t>(count), kInfinity);
        std::vector<int> parent(static_cast<size_t>(count), -1);
        // Сколько сухопутных шагов пройдено от истока.
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

        distance[static_cast<size_t>(source)] = 0.0f;
        queue.push({ 0.0f, source });

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
                    // Море — только устье: от истока до него должен быть путь по суше.
                    if (current == source || !isCoastalSeaCell(model, neighbor)) {
                        continue;
                    }
                    const int landCells = landDepth[static_cast<size_t>(current)] + 1;
                    if (landCells < options.minLandCells
                        || landCells + 1 > options.maxRiverCells) {
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
            if (current == source) {
                break;
            }
        }
        std::reverse(path.begin(), path.end());

        if (path.size() < 3 || path.front() != source) {
            return {};
        }
        const int landCells = static_cast<int>(path.size()) - 1;
        if (landCells < options.minLandCells
            || static_cast<int>(path.size()) > options.maxRiverCells) {
            return {};
        }

        const Cell& sourceCell = cells[static_cast<size_t>(source)];
        const Cell& mouthCell = cells[static_cast<size_t>(goal)];
        if (sourceCell.height - mouthCell.height < options.minSourceDrop) {
            return {};
        }

        // Река не должна по дороге забираться выше своего истока: иначе это не
        // сток с горы, а случайный маршрут через хребет. Устье (последняя клетка)
        // лежит в море и в проверку не входит.
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            const Cell& cell = cells[static_cast<size_t>(path[i])];
            if (cell.biome == Biome::Sea || cell.height > sourceCell.height) {
                return {};
            }
        }
        if (cells[static_cast<size_t>(path.back())].biome != Biome::Sea) {
            return {};
        }

        RiverPath river;
        river.cellIds = std::move(path);
        river.sourceCellId = source;
        river.mouthCellId = goal;
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

    const std::vector<SourceCandidate> sources = collectMountainSources(model, options);
    if (sources.empty()) {
        return rivers;
    }

    std::vector<char> blocked(static_cast<size_t>(count), 0);

    for (const SourceCandidate& source : sources) {
        if (static_cast<int>(rivers.size()) >= options.maxRivers) {
            break;
        }
        if (blocked[static_cast<size_t>(source.cellId)]) {
            continue;
        }

        RiverPath river = findRiverFromSource(model, source.cellId, blocked, options);
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
