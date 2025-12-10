#include "OreSystem.h"
#include <algorithm>
#include <numeric>
#include <cmath>

OreSystem::OreSystem() : rng_(std::random_device{}()) {
    oreColors_[1] = QVector3D(0.7f, 0.4f, 0.2f);  // Железо
    oreColors_[2] = QVector3D(0.8f, 0.5f, 0.2f);  // Медь
    oreColors_[3] = QVector3D(0.9f, 0.9f, 0.1f);  // Золото
    oreColors_[4] = QVector3D(0.4f, 0.4f, 0.8f);  // Алмаз
}

void OreSystem::initialize(HexSphereModel& model) {
    model_ = &model;
    deposits_.clear();

    // Автоматическое создание месторождений
    const auto& cells = model_->cells();
    for (const auto& cell : cells) {
        if (cell.oreType > 0 && cell.oreDensity > 0.1f) {
            addDeposit(cell.id, cell.oreDensity);
        }
    }

    hasChanges_ = true;
}

void OreSystem::update(float deltaTime) {
    if (!model_ || deposits_.empty()) return;

    timeAccumulator_ += deltaTime;
    hasChanges_ = false;

    // Обновляем каждые 0.1 секунды
    if (timeAccumulator_ >= 0.1f) {
        for (auto& deposit : deposits_) {
            if (!deposit.active) continue;

            float oldDensity = deposit.density;
            deposit.density += (deposit.targetDensity - deposit.density) *
                deposit.growthRate * globalGrowthRate_;
            deposit.density = std::clamp(deposit.density, 0.0f, 1.0f);

            if (std::abs(deposit.density - oldDensity) > 0.001f) {
                model_->setOreDensity(deposit.cellId, deposit.density);
                hasChanges_ = true;
            }
        }

        // Диффузия между соседними месторождениями
        diffuseOreDensity();

        // Обновляем визуальные параметры
        updateVisualParams();

        timeAccumulator_ = 0.0f;
    }
}

void OreSystem::addDeposit(int cellId, float initialDensity) {
    if (!model_ || cellId < 0 || cellId >= model_->cellCount()) return;

    auto it = std::find_if(deposits_.begin(), deposits_.end(),
        [cellId](const OreDeposit& d) { return d.cellId == cellId; });

    if (it == deposits_.end()) {
        OreDeposit deposit;
        deposit.cellId = cellId;
        deposit.density = initialDensity;
        deposit.targetDensity = initialDensity;

        std::uniform_real_distribution<float> dist(0.05f, 0.2f);
        deposit.growthRate = dist(rng_);

        deposits_.push_back(deposit);
        hasChanges_ = true;
    }
}

void OreSystem::removeDeposit(int cellId) {
    deposits_.erase(
        std::remove_if(deposits_.begin(), deposits_.end(),
            [cellId](const OreDeposit& d) { return d.cellId == cellId; }),
        deposits_.end());
    hasChanges_ = true;
}

void OreSystem::clearAllDeposits() {
    deposits_.clear();
    hasChanges_ = true;
}

float OreSystem::getAverageDensity() const {
    if (deposits_.empty()) return 0.0f;
    float sum = 0.0f;
    for (const auto& deposit : deposits_) sum += deposit.density;
    return sum / deposits_.size();
}

// Реализация diffuseOreDensity
void OreSystem::diffuseOreDensity() {
    if (deposits_.size() < 2 || diffusionRate_ <= 0.0f) return;

    const float diffusionAmount = 0.01f * diffusionRate_;

    for (auto& deposit : deposits_) {
        if (!deposit.active) continue;

        const auto& cell = model_->cells()[deposit.cellId];
        for (int neighborId : cell.neighbors) {
            if (neighborId < 0) continue;

            auto neighborIt = std::find_if(deposits_.begin(), deposits_.end(),
                [neighborId](const OreDeposit& d) { return d.cellId == neighborId; });

            if (neighborIt != deposits_.end() && neighborIt->active) {
                float diff = deposit.density - neighborIt->density;
                float transfer = diff * diffusionAmount;

                deposit.density -= transfer;
                neighborIt->density += transfer;

                deposit.density = std::clamp(deposit.density, 0.0f, 1.0f);
                neighborIt->density = std::clamp(neighborIt->density, 0.0f, 1.0f);
            }
        }
    }
}

// Реализация updateVisualParams
void OreSystem::updateVisualParams() {
    // Пустая реализация - можно добавить обновление визуальных параметров
    // Например, обновление цвета руды в модели
}