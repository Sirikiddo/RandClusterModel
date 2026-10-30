#include "tests/RiverGeneratorTests.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "generation/RiverGenerator.h"
#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

namespace {

    void require(bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    int landHeightQuantile(const HexSphereModel& model, float quantile) {
        std::vector<int> heights;
        heights.reserve(model.cells().size());
        for (const Cell& cell : model.cells()) {
            if (cell.biome != Biome::Sea) {
                heights.push_back(cell.height);
            }
        }
        if (heights.empty()) {
            return 0;
        }
        const size_t index = std::min(
            heights.size() - 1,
            static_cast<size_t>(quantile * static_cast<float>(heights.size() - 1)));
        std::nth_element(heights.begin(), heights.begin() + static_cast<ptrdiff_t>(index), heights.end());
        return heights[index];
    }

    struct TestPlanet {
        HexSphereModel model;
        RiverGenerationOptions options;
        std::vector<RiverPath> rivers;
    };

    TestPlanet makePlanet(int subdivision, uint32_t seed) {
        TestPlanet planet;
        IcosphereBuilder builder;
        planet.model.rebuildFromIcosphere(builder.build(subdivision));

        auto generator = createTerrainGeneratorByIndex(kDefaultTerrainGeneratorIndex);
        TerrainParams params = defaultTerrainParams();
        params.seed = seed;
        generateCanonicalTerrain(*generator, planet.model, params);

        planet.options.seed = seed ^ (static_cast<uint32_t>(subdivision + 1) * 0x85ebca6bu)
            ^ 0x52e3f1a7u;
        planet.options.maxRivers = std::clamp(
            static_cast<int>(planet.model.cells().size()) / 40, 2, 24);
        planet.options.maxRiverCells = std::clamp(12 + 4 * subdivision, 16, 40);
        planet.options.minLandCells = 2;
        planet.options.minSourceDrop = 2;
        planet.options.searchNodeLimit = 3000;
        planet.rivers = RiverGenerator::generate(planet.model, planet.options);
        return planet;
    }

    void testRiversFlowFromMountainsToSea() {
        const int subdivisions[] = { 1, 2, 3 };
        const uint32_t seeds[] = { 0u, 1u, 2u, 42u, 12345u };

        int totalRivers = 0;
        for (int subdivision : subdivisions) {
            for (uint32_t seed : seeds) {
                const TestPlanet planet = makePlanet(subdivision, seed);
                const auto& cells = planet.model.cells();
                const int highThreshold = landHeightQuantile(planet.model, 0.85f);
                const int topThreshold = landHeightQuantile(planet.model, 0.95f);

                require(!planet.rivers.empty(),
                    "no rivers for subdivision " + std::to_string(subdivision)
                    + " seed " + std::to_string(seed));

                std::set<int> occupied;
                for (const RiverPath& river : planet.rivers) {
                    require(river.isValid(), "river is too short");
                    require(river.sourceCellId == river.cellIds.front(),
                        "river source id does not match the first cell");
                    require(river.mouthCellId == river.cellIds.back(),
                        "river mouth id does not match the last cell");
                    require(static_cast<int>(river.cellIds.size()) <= planet.options.maxRiverCells,
                        "river is longer than the configured limit");

                    const Cell& source = cells[static_cast<size_t>(river.cellIds.front())];
                    const Cell& mouth = cells[static_cast<size_t>(river.cellIds.back())];
                    require(source.biome != Biome::Sea, "river starts in the sea");
                    require(mouth.biome == Biome::Sea, "river does not end in the sea");

                    const bool mouthTouchesLand = std::any_of(
                        mouth.neighbors.begin(), mouth.neighbors.end(),
                        [&cells](int neighbor) {
                            return neighbor >= 0
                                && neighbor < static_cast<int>(cells.size())
                                && cells[static_cast<size_t>(neighbor)].biome != Biome::Sea;
                        });
                    require(mouthTouchesLand, "river mouth is not on the coast");

                    for (size_t i = 0; i + 1 < river.cellIds.size(); ++i) {
                        const Cell& from = cells[static_cast<size_t>(river.cellIds[i])];
                        const int next = river.cellIds[i + 1];
                        require(std::find(from.neighbors.begin(), from.neighbors.end(), next)
                            != from.neighbors.end(),
                            "river path contains non-adjacent cells");
                    }

                    const bool mountainBiome =
                        source.biome == Biome::Rock || source.biome == Biome::Snow;
                    require(mountainBiome || source.height >= topThreshold,
                        "river source is not a mountain cell");
                    require(source.height >= highThreshold,
                        "river source is below the mountain height threshold");
                    require(source.height - mouth.height >= planet.options.minSourceDrop,
                        "river does not descend from source to mouth");

                    for (size_t i = 1; i < river.cellIds.size(); ++i) {
                        const Cell& cell = cells[static_cast<size_t>(river.cellIds[i])];
                        if (i + 1 < river.cellIds.size()) {
                            require(cell.biome != Biome::Sea,
                                "river crosses the sea before its mouth");
                        }
                        require(cell.height <= source.height,
                            "river climbs above its source");
                    }

                    for (int cellId : river.cellIds) {
                        require(occupied.insert(cellId).second,
                            "rivers overlap at a cell");
                    }
                }
                totalRivers += static_cast<int>(planet.rivers.size());
            }
        }
        require(totalRivers > 0, "river generation produced no rivers at all");
    }

} // namespace

void runRiverGeneratorUnitTests() {
    testRiversFlowFromMountainsToSea();
}
