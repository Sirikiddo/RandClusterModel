#pragma once
#include "model/HexSphereModel.h"
#include <vector>
#include <memory>
#include <random>
#include <unordered_map>

class OreSystem {
public:
    struct OreDeposit {
        int cellId;
        float density;           // Текущая плотность [0-1]
        float targetDensity;     // Целевая плотность [0-1]
        float growthRate;        // Скорость роста/убывания
        bool active = true;      // Активно ли месторождение
    };

    OreSystem();

    // Инициализация системы
    void initialize(HexSphereModel& model);

    // Обновление системы (вызывается каждый кадр/таймер)
    void update(float deltaTime);

    // Добавление/удаление месторождений
    void addDeposit(int cellId, float initialDensity = 0.5f);
    void removeDeposit(int cellId);
    void clearAllDeposits();

    // Настройка параметров
    void setGlobalGrowthRate(float rate) { globalGrowthRate_ = rate; }
    void setDiffusionRate(float rate) { diffusionRate_ = rate; }

    // Проверка изменений
    bool hasChanges() const { return hasChanges_; }
    void resetChanges() { hasChanges_ = false; }

    // Получение информации
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

    // Карта цветов для разных типов руд
    std::unordered_map<uint8_t, QVector3D> oreColors_;

    // Диффузия плотности между соседними ячейками
    void diffuseOreDensity();

    // Обновление визуальных параметров в модели
    void updateVisualParams();
};