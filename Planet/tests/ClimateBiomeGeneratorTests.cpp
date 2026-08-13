#include "tests/ClimateBiomeGeneratorTests.h"

#include <algorithm>
#include <stdexcept>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testSeaLevelDatum() {
    ClimateBiomeTerrainGenerator generator;
    for (uint32_t seed : { 0u, 1u, 2u, 42u, 12345u }) {
        HexSphereModel model;
        IcosphereBuilder builder;
        model.rebuildFromIcosphere(builder.build(kDefaultTerrainSubdivisionLevel));

        TerrainParams params = defaultTerrainParams();
        params.seed = seed;
        generateCanonicalTerrain(generator, model, params);

        const auto seaCount = std::count_if(
            model.cells().begin(), model.cells().end(),
            [](const Cell& cell) { return cell.biome == Biome::Sea; });
        const float seaFraction = static_cast<float>(seaCount)
            / static_cast<float>(model.cells().size());
        require(seaFraction >= 0.05f,
            "climate sea level zero produced too little water");
        require(seaFraction <= 0.35f,
            "climate sea level zero submerged too much terrain");
    }
}

} // namespace

void runClimateBiomeGeneratorUnitTests() {
    testSeaLevelDatum();
}
