#pragma once
#include "HexSphereModel.h"
#include <QVector3D>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <optional>
#include <algorithm>

struct TerrainMesh {
    std::vector<float>    pos; // xyz...
    std::vector<float>    col; // rgb...
    std::vector<float>    norm; // нормали: nx,ny,nz... (ДОБАВЛЕНО из версии 2)
    std::vector<uint32_t> idx; // indices
    std::vector<int> triOwner;   // k, владелец треугольника
};

class TerrainTessellator {
public:
    // Параметры
    float R = 1.0f;
    float heightStep = 0.05f;
    float inset = 0.25f;     // inner в сторону центроида [0..0.49]
    float outerTrim = 0.18f; // отступ вдоль внешних рёбер [0..0.49]
    int   smoothMaxDelta = 0;

    // Порог чувствительности клифов (после сопоставления двух сторон ребра)
    float epsEdge = 1e-6f;   // разница уровней лопасти вдоль ребра
    float epsApex = 1e-6f;   // разница апексов на концах ребра

    // Флаги этапов
    bool doCaps = true;
    bool doBlades = true;
    bool doCornerTris = true;
    bool doEdgeCliffs = true; // пост-проход

    TerrainMesh build(const HexSphereModel& model) const;

public:
    // ── атомарные утилиты ────────────────────────────────────────────────────
    static QVector3D slerpish(const QVector3D& a, const QVector3D& b, float t);
    QVector3D        liftUnit(const QVector3D& unit, float h) const;

    enum class EdgeMode { Flat, Slope, Cliff };
    static EdgeMode  classifyEdge(int hA, int hB, int smoothMaxDelta);

    static int       findLocalIndex(const Cell& c, int dv);
    float            bladeHeightForEdge(const Cell& c, int edgeIdx,
        const std::vector<Cell>& cells) const;
    float            cornerBlendTargetHeight(const Cell& c, int i,
        const std::vector<Cell>& cells) const;

    // ── подготовка клетки ────────────────────────────────────────────────────
    struct PreCell {
        std::vector<QVector3D> inner;
        std::vector<QVector3D> outerUnit;
        QVector3D              center;
        QVector3D              color;
        float                  h = 0.f;
    };
    PreCell makePreCell(const Cell& c, const std::vector<QVector3D>& dual) const;

    struct TrimDirs {
        std::vector<QVector3D> sideL, sideR; // по ребру i→j (смещённые trim)
        std::vector<QVector3D> prevU, currU; // у угла i: к предыдущему / текущему ребру
        std::vector<QVector3D> apexU;        // на сам угол i
    };
    TrimDirs makeTrimDirs(const PreCell& pc) const;

    struct EdgeHeights {
        std::vector<float> edgeH;  // уровень лопасти на ребре i
        std::vector<float> apexH;  // уровень апекса в угле i
    };
    EdgeHeights makeHeights(const Cell& c, const PreCell& pc,
        const std::vector<Cell>& cells) const;

    struct MeshBuilder {
        std::vector<float>& pos;
        std::vector<float>& col;
        std::vector<float>& norm; // ДОБАВЛЕНО из версии 2
        std::vector<uint32_t>& idx;
        std::vector<int>* owner = nullptr;
        std::vector<QVector3D> dbgNormals;
        void triToward(QVector3D A, QVector3D B, QVector3D C,
            const QVector3D& color, const QVector3D& toward,
            int cellOwner);
        void quadToward(const QVector3D& Q0, const QVector3D& Q1,
            const QVector3D& Q2, const QVector3D& Q3,
            const QVector3D& color, const QVector3D& toward,
            int cellOwner);
    };

    // ── "мягкие" этапы ───────────────────────────────────────────────────────
    void buildInnerFan(MeshBuilder& mb, const Cell& c, const PreCell& pc) const;
    void buildBlades(MeshBuilder& mb, const Cell& c, const PreCell& pc,
        const TrimDirs& td, const EdgeHeights& eh) const;
    void buildCorners(MeshBuilder& mb, const Cell& c, const PreCell& pc,
        const TrimDirs& td, const EdgeHeights& eh) const;

    // ── реестр рёбер для пост-прохода ────────────────────────────────────────
    struct EdgeKey {
        int v0, v1;
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
        int       cellId = -1;
        int       hCell = 0;
        float     Hedge = 0.f;   // уровень лопасти вдоль ребра
        float     Hleft = 0.f;   // уровень лопасти на предыдущем ребре (i-1)
        float     Hright = 0.f;   // уровень лопасти у правого конца (текущее ребро)
        float     apexL = 0.f;   // апекс в левом конце (угол i)
        float     apexR = 0.f;   // апекс в правом конце (угол j)
        QVector3D sideL, sideR;   // unit вдоль ребра ближе к i/j
        QVector3D prevU_L;        // unit от левого угла к предыдущему ребру
        QVector3D currU_R;        // unit от правого угла к текущему ребру
        QVector3D apexDirL, apexDirR; // unit на левый/правый апекс
        QVector3D centroid;       // центроид клетки (unit)

        QVector3D P_edgeL, P_edgeR;    // blade points на текущем ребре
        QVector3D P_prevS, P_currS;    // «борта» у левого/правого угла
        QVector3D P_apexL, P_apexR;    // RAW апексы (для Slope)
        QVector3D P_apexLCap, P_apexRCap; // CAP апексы (кламп к hCell, для Cliff)
    };
    struct EdgeRec { std::optional<EdgeSide> A, B; };
    using EdgeRegistry = std::unordered_map<EdgeKey, EdgeRec, EdgeKeyHash>;

    void registerEdgeSide(EdgeRegistry& reg, size_t cid,
        const Cell& c, int iEdge,
        const PreCell& pc, const TrimDirs& td, const EdgeHeights& eh) const;

    void finalizeCliffs(const EdgeRegistry& reg,
        MeshBuilder& mb,
        const std::vector<Cell>& cells) const;
};