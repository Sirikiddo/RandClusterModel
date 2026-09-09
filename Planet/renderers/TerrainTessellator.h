#pragma once

#include <QVector3D>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

enum class TriangleSurfaceRole : uint8_t {
    Top,
    Slope,
    Cliff,
    Skirt,
};

struct TerrainMesh {
    std::vector<float> pos;
    std::vector<float> col;
    std::vector<float> norm;
    std::vector<float> ore;
    std::vector<uint32_t> idx;
    std::vector<int> triOwner;
    std::vector<TriangleSurfaceRole> triSurfaceRole;
    int beachTriCount = 0;
};

class TerrainTessellator {
public:
    float inset = 0.25f;
    float outerTrim = 0.18f;
    int smoothMaxDelta = 0;

    float epsEdge = 1e-6f;
    float epsApex = 1e-6f;

    bool doCaps = true;
    bool doBlades = true;
    bool doCornerTris = true;
    bool doEdgeCliffs = true;
    const CoastalBandData* coastalBand = nullptr;
    const WaterParams* waterParams = nullptr;

    bool enableOreVisualization = true;
    float oreAnimationSpeed = 0.1f;
    static void updateOreData(TerrainMesh& mesh, const HexSphereModel& model);

    class OreNoiseGenerator {
    public:
        explicit OreNoiseGenerator(uint32_t seed = 12345);
        float noise(float x, float y, float z, float time = 0.0f) const;

    private:
        static float fade(float t);
        static float lerp(float a, float b, float t);
        static float grad(int hash, float x, float y, float z);

        std::array<int, 512> perm_;
    };

    void updateAnimation(float deltaTime);
    void setOreAnimationTime(float time) { animationTime_ = time; }
    void setOreVisualizationEnabled(bool enabled) { enableOreVisualization = enabled; }

    TerrainMesh build(const HexSphereModel& model) const;

public:
    static QVector3D slerpish(const QVector3D& a, const QVector3D& b, float t);
    QVector3D liftUnit(const QVector3D& unit, float h) const;

    enum class EdgeMode { Flat, Slope, Cliff };
    static EdgeMode classifyEdge(int hA, int hB, int smoothMaxDelta);

    static int findLocalIndex(const Cell& c, int dv);
    float bladeHeightForEdge(const Cell& c, int edgeIdx, const std::vector<Cell>& cells) const;
    float cornerBlendTargetHeight(const Cell& c, int i, const std::vector<Cell>& cells) const;

    QVector3D calculateCellColorWithOre(
        const Cell& cell,
        const QVector3D& baseColor,
        const QVector3D& position) const;

    QVector3D colorForCell(const Cell& c) const;
    QVector3D cliffColorForEdge(const Cell& c) const;
    bool isSeaEdge(const Cell& c, int edgeIdx, const std::vector<Cell>& cells) const;
    bool isBeachLikeCell(const Cell& c) const;
    float beachBlendForCell(const Cell& c) const;

    struct PreCell {
        std::vector<QVector3D> inner;
        std::vector<QVector3D> outerUnit;
        QVector3D center;
        QVector3D color;
        float h = 0.0f;
        bool beachLike = false;
    };
    PreCell makePreCell(const Cell& c, const std::vector<QVector3D>& dual) const;

    struct TrimDirs {
        std::vector<QVector3D> sideL, sideR;
        std::vector<QVector3D> prevU, currU;
        std::vector<QVector3D> apexU;
    };
    TrimDirs makeTrimDirs(const PreCell& pc) const;

    struct EdgeHeights {
        std::vector<float> edgeH;
        std::vector<float> apexH;
    };
    EdgeHeights makeHeights(const Cell& c, const PreCell& pc, const std::vector<Cell>& cells) const;

    struct MeshBuilder {
        std::vector<float>& pos;
        std::vector<float>& col;
        std::vector<float>& norm;
        std::vector<float>& ore;
        std::vector<uint32_t>& idx;
        std::vector<int>* owner = nullptr;
        std::vector<TriangleSurfaceRole>* surfaceRole = nullptr;
        int* beachTriCount = nullptr;
        const std::vector<Cell>* cells = nullptr;

        void triToward(
            QVector3D A,
            QVector3D B,
            QVector3D C,
            const QVector3D& color,
            const QVector3D& toward,
            int cellOwner,
            TriangleSurfaceRole role = TriangleSurfaceRole::Top);
        void quadToward(
            const QVector3D& Q0,
            const QVector3D& Q1,
            const QVector3D& Q2,
            const QVector3D& Q3,
            const QVector3D& color,
            const QVector3D& toward,
            int cellOwner,
            TriangleSurfaceRole role = TriangleSurfaceRole::Top);
    };

    void buildInnerFan(MeshBuilder& mb, const Cell& c, const PreCell& pc) const;
    void buildBlades(MeshBuilder& mb, const Cell& c, const PreCell& pc, const TrimDirs& td, const EdgeHeights& eh) const;
    void buildCorners(MeshBuilder& mb, const Cell& c, const PreCell& pc, const TrimDirs& td, const EdgeHeights& eh) const;

    struct EdgeKey {
        int v0;
        int v1;
        bool operator==(const EdgeKey& other) const noexcept {
            return v0 == other.v0 && v1 == other.v1;
        }
    };
    struct EdgeKeyHash {
        size_t operator()(const EdgeKey& k) const noexcept {
            return (size_t(uint32_t(k.v0)) << 32) ^ uint32_t(k.v1);
        }
    };

    struct EdgeSide {
        int cellId = -1;
        int hCell = 0;
        float Hedge = 0.0f;
        float Hleft = 0.0f;
        float Hright = 0.0f;
        float apexL = 0.0f;
        float apexR = 0.0f;
        QVector3D sideL, sideR;
        QVector3D prevU_L;
        QVector3D currU_R;
        QVector3D apexDirL, apexDirR;
        QVector3D centroid;
        bool beachLike = false;

        QVector3D P_edgeL, P_edgeR;
        QVector3D P_apexL, P_apexR;
    };
    struct EdgeRec {
        std::optional<EdgeSide> A;
        std::optional<EdgeSide> B;
    };
    using EdgeRegistry = std::unordered_map<EdgeKey, EdgeRec, EdgeKeyHash>;

    void registerEdgeSide(
        EdgeRegistry& reg,
        size_t cid,
        const Cell& c,
        int iEdge,
        const PreCell& pc,
        const TrimDirs& td,
        const EdgeHeights& eh) const;

    void finalizeCliffs(
        const EdgeRegistry& reg,
        MeshBuilder& mb,
        const std::vector<Cell>& cells) const;

private:
    OreNoiseGenerator oreNoise_{ 12345 };
    mutable float animationTime_ = 0.0f;
    mutable const HexSphereModel* surfaceModel_ = nullptr;
};
