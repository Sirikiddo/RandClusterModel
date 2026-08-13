#pragma once

#include "model/HexSphereModel.h"

#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

class OreSystem {
public:
    struct OreDeposit {
        int cellId;
        float density;           // Текущая плотность [0-1]
        float targetDensity;     // Целевая плотность [0-1]
        float growthRate;        // Скорость роста/убывания
        bool active = true;
    };

    OreSystem();

    void initialize(HexSphereModel& model);
    void update(float deltaTime);

    void addDeposit(int cellId, float initialDensity = 0.5f);
    void removeDeposit(int cellId);
    void clearAllDeposits();

    void setGlobalGrowthRate(float rate) { globalGrowthRate_ = rate; }
    void setDiffusionRate(float rate) { diffusionRate_ = rate; }

    bool hasChanges() const { return hasChanges_; }
    void resetChanges() { hasChanges_ = false; }

    size_t getDepositCount() const { return deposits_.size(); }
    float getAverageDensity() const;

private:
    std::vector<OreDeposit> deposits_;
    HexSphereModel* model_ = nullptr;
    std::mt19937 rng_;

    float globalGrowthRate_ = 0.1f;
    float diffusionRate_ = 0.05f;
    float timeAccumulator_ = 0.0f;
    bool hasChanges_ = false;

    std::unordered_map<uint8_t, QVector3D> oreColors_;

    void diffuseOreDensity();
    void updateVisualParams();
};
