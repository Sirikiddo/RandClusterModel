#pragma once
#include <cstdint>
#include <memory>

class HexSphereModel;

struct TerrainParams {
    uint32_t seed = 0u;
    int      seaLevel = 0;
    float    scale = 1.0f;
};

class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;
    virtual void generate(HexSphereModel& model, const TerrainParams& p) = 0;
};

// Ничего не делает: всё «по умолчанию»
class NoOpTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

// Синусоидальная генерация
class SineTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

// Шум Перлина
class PerlinTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};

// Климатическая генерация биомов
class ClimateBiomeTerrainGenerator final : public ITerrainGenerator {
public:
    void generate(HexSphereModel& model, const TerrainParams& p) override;
};