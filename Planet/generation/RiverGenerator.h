#pragma once

#include <cstdint>
#include <vector>

#include "model/HexSphereModel.h"

// Река — путь по клеткам планеты, который начинается и заканчивается в клетках
// моря, а между ними идёт по суше. Формально это тот же "путь", что и дорога,
// но с речными правилами: море допустимо только на концах, подъём невыгоден,
// а через уже проложенные реки новые не текут.
struct RiverPath {
    // front()/back() — клетки моря (устья), середина — суша.
    std::vector<int> cellIds;

    bool isValid() const noexcept {
        return cellIds.size() >= 3;
    }
};

struct RiverGenerationOptions {
    uint32_t seed = 0u;

    // Сколько рек максимум пытаться проложить.
    int maxRivers = 4;

    // Максимальная длина реки в клетках, включая оба устья.
    int maxRiverCells = 18;

    // Минимум сухопутных клеток между устьями: река из одного перешейка не нужна.
    int minLandCells = 2;

    // Ограничение на число посещённых клеток в одном поиске (защита от долгого A*).
    int searchNodeLimit = 600;
};

class RiverGenerator {
public:
    // Детерминированная генерация рек по текущей топологии и биомам модели.
    static std::vector<RiverPath> generate(
        const HexSphereModel& model,
        const RiverGenerationOptions& options);
};
