#include "tests/SelectionOutlineTests.h"

#include <cmath>
#include <stdexcept>

#include <QSet>

#include "dag/DagSceneBackend.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float lhs, float rhs, float epsilon = 1.0e-5f) {
    return std::abs(lhs - rhs) <= epsilon;
}

bool sameVertices(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!close(lhs[i], rhs[i])) return false;
    }
    return true;
}

HexSphereModel buildModel(int subdivisionLevel) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(subdivisionLevel));
    return model;
}

std::pair<int, int> findPentagonAndHexagon(const HexSphereModel& model) {
    int pentagon = -1;
    int hexagon = -1;
    for (size_t i = 0; i < model.cells().size(); ++i) {
        if (model.cells()[i].poly.size() == 5 && pentagon < 0) pentagon = int(i);
        if (model.cells()[i].poly.size() == 6 && hexagon < 0) hexagon = int(i);
    }
    require(pentagon >= 0 && hexagon >= 0, "test topology has no pentagon or hexagon");
    return { pentagon, hexagon };
}

SelectionOutlineInput makeInput(
    const HexSphereModel& model,
    std::initializer_list<int> ids,
    float bias = 0.004f,
    bool smooth = true) {
    QSet<int> selected;
    for (int id : ids) selected.insert(id);
    return SelectionOutlineGenerator::buildSelectionOutlineInput(model, selected, 0.05f, bias, smooth);
}

void testExtractionAndOrdering() {
    const HexSphereModel model = buildModel(2);
    const auto [pentagon, hexagon] = findPentagonAndHexagon(model);
    require(makeInput(model, { pentagon }).edges.size() == 5, "pentagon must produce five edges");
    require(makeInput(model, { hexagon }).edges.size() == 6, "hexagon must produce six edges");
    require(makeInput(model, {}).edges.empty(), "empty selection must produce empty input");
    require(makeInput(model, { -1, int(model.cells().size()) }).edges.empty(), "invalid IDs must be ignored");

    QSet<int> first;
    first.insert(hexagon);
    first.insert(pentagon);
    QSet<int> second;
    second.insert(pentagon);
    second.insert(hexagon);
    const auto a = SelectionOutlineGenerator::buildSelectionOutlineInput(model, first, 0.05f, 0.004f, true);
    const auto b = SelectionOutlineGenerator::buildSelectionOutlineInput(model, second, 0.05f, 0.004f, true);
    require(a.edges.size() == 11, "two cells must produce the sum of their edges");
    require(serializeSelectionOutlineInput(a) == serializeSelectionOutlineInput(b), "selection ordering must be stable");
}

void testGeneratorAndDagEquivalence() {
    HexSphereModel model = buildModel(2);
    const auto [pentagon, hexagon] = findPentagonAndHexagon(model);
    model.cells()[size_t(pentagon)].height = 3;
    model.cells()[size_t(hexagon)].height = 4;

    const std::vector<SelectionOutlineInput> inputs = {
        makeInput(model, {}),
        makeInput(model, { pentagon }),
        makeInput(model, { hexagon }),
        makeInput(model, { pentagon, hexagon }),
        makeInput(model, { pentagon, hexagon }, 0.012f, true),
        makeInput(model, { pentagon, hexagon }, 0.004f, false),
    };
    DagSceneBackend backend;
    for (const auto& input : inputs) {
        const SelectionDagResult result = backend.rebuildSelectionOutline(input);
        require(result.success, "valid selection DAG request failed");
        require(sameVertices(result.vertices, SelectionOutlineGenerator::buildSelectionOutlineVertices(input)),
            "direct and DAG selection geometry differ");
    }
}

void testSmoothHeight() {
    SelectionOutlineEdge edge;
    edge.startUnit = QVector3D(1.0f, 0.0f, 0.0f);
    edge.endUnit = QVector3D(0.0f, 1.0f, 0.0f);
    edge.cellHeight = 2;
    edge.neighborHeight = 3;
    edge.hasNeighbor = true;

    SelectionOutlineInput input;
    input.edges.push_back(edge);
    input.heightStep = 0.1f;
    input.smoothOneStep = true;
    const auto smooth = SelectionOutlineGenerator::buildSelectionOutlineVertices(input);
    input.smoothOneStep = false;
    const auto hard = SelectionOutlineGenerator::buildSelectionOutlineVertices(input);
    require(close(smooth[0], 1.25f), "smooth one-step edge must use average height");
    require(close(hard[0], 1.2f), "non-smooth edge must use selected cell height");
}

void testSerializationAndInputSize() {
    const HexSphereModel l2 = buildModel(2);
    const HexSphereModel l4 = buildModel(4);
    const int l2Hex = findPentagonAndHexagon(l2).second;
    const int l4Hex = findPentagonAndHexagon(l4).second;
    const SelectionOutlineInput original = makeInput(l2, { l2Hex }, 0.007f, false);
    const auto decoded = deserializeSelectionOutlineInput(serializeSelectionOutlineInput(original));
    require(decoded.has_value(), "selection serialization round-trip failed");
    require(decoded->edges.size() == original.edges.size(), "round-trip changed edge count");
    require(close(decoded->heightStep, original.heightStep) && close(decoded->outlineBias, original.outlineBias)
        && decoded->smoothOneStep == original.smoothOneStep, "round-trip changed parameters");
    for (size_t i = 0; i < original.edges.size(); ++i) {
        require((decoded->edges[i].startUnit - original.edges[i].startUnit).length() < 1.0e-5f,
            "round-trip changed edge vectors");
        require(decoded->edges[i].cellHeight == original.edges[i].cellHeight
            && decoded->edges[i].neighborHeight == original.edges[i].neighborHeight
            && decoded->edges[i].hasNeighbor == original.edges[i].hasNeighbor,
            "round-trip changed edge metadata");
    }

    const int bytesL2 = serializeSelectionOutlineInput(original).toUtf8().size();
    const SelectionOutlineInput inputL4 = makeInput(l4, { l4Hex }, 0.007f, false);
    const int bytesL4 = serializeSelectionOutlineInput(inputL4).toUtf8().size();
    require(original.edges.size() == inputL4.edges.size(), "L2/L4 hex selections changed edge count");
    require(std::abs(bytesL2 - bytesL4) < 128, "selection input size scales with total cell count");
}

void testSelectiveExecutionAndGuards() {
    HexSphereModel model = buildModel(2);
    const auto [pentagon, hexagon] = findPentagonAndHexagon(model);
    SelectionOutlineInput input = makeInput(model, { hexagon });
    DagSceneBackend backend;

    const SelectionDagResult first = backend.rebuildSelectionOutline(input);
    require(first.success && backend.lastStats().executedNodes == 1,
        "first selection request must execute exactly one node");
    const auto expected = first.vertices;
    const SelectionDagResult repeat = backend.rebuildSelectionOutline(input);
    require(repeat.success && backend.lastStats().executedNodes == 0
        && backend.lastStats().skippedGuardNodes == 1 && sameVertices(repeat.vertices, expected),
        "identical selection request must reuse persistent output");

    input = makeInput(model, { pentagon });
    backend.rebuildSelectionOutline(input);
    require(backend.lastStats().executedNodes == 1, "selection change must execute only selection node");
    input.outlineBias += 0.001f;
    backend.rebuildSelectionOutline(input);
    require(backend.lastStats().executedNodes == 1, "bias change must execute selection node");
    input.smoothOneStep = !input.smoothOneStep;
    backend.rebuildSelectionOutline(input);
    require(backend.lastStats().executedNodes == 1, "smooth change must execute selection node");
    input.edges.front().cellHeight += 1;
    backend.rebuildSelectionOutline(input);
    require(backend.lastStats().executedNodes == 1, "height change must execute selection node");
}

void testSceneDerivedIsolation() {
    const HexSphereModel model = buildModel(2);
    TerrainSnapshot snapshot;
    snapshot.subdivisionLevel = 2;
    snapshot.cells.resize(model.cells().size());
    SceneDagRequest request;
    request.terrain = snapshot;
    request.heightStep = 0.05f;
    request.modelRequests.push_back(ModelPlacementRequest{ 1, "pyramid", 0, false, 0.0f });

    DagSceneBackend backend;
    const SceneDagResult first = backend.rebuild(request);
    require(backend.lastStats().executedNodes == 2, "scene-derived request must execute tree/model nodes only");
    const SceneDagResult repeat = backend.rebuild(request);
    require(backend.lastStats().executedNodes == 0 && backend.lastStats().skippedGuardNodes == 2,
        "repeat scene-derived request must preserve tree/model guards");
    require(first.treePlacements.size() == repeat.treePlacements.size()
        && first.modelPlacements.size() == repeat.modelPlacements.size(),
        "scene-derived repeat changed persistent placements");
}

} // namespace

void runSelectionOutlineUnitTests() {
    testExtractionAndOrdering();
    testGeneratorAndDagEquivalence();
    testSmoothHeight();
    testSerializationAndInputSize();
    testSelectiveExecutionAndGuards();
    testSceneDerivedIsolation();
}
