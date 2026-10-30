#pragma once

#include <cstdint>
#include <vector>

#include "model/HexSphereModel.h"

// Река — путь по клеткам планеты, который берёт начало в горах и впадает в море.
// Формально это тот же "путь", что и дорога, но с речными правилами: исток
// выбирается на высоких горных клетках, течение идёт вниз, море допустимо
// только в устье (последней клетке), а через уже проложенные реки новые не текут.
struct RiverPath {
    // front() — горный исток (суша), back() — морское устье. Между ними суша.
    std::vector<int> cellIds;
    int sourceCellId = -1;
    int mouthCellId = -1;

    bool isValid() const noexcept {
        return cellIds.size() >= 3;
    }
};

struct RiverGenerationOptions {
    uint32_t seed = 0u;

    // Сколько рек максимум пытаться проложить.
    int maxRivers = 4;

    // Максимальная длина реки в клетках, включая исток и устье.
    int maxRiverCells = 18;

    // Минимум сухопутных клеток между истоком и морем.
    int minLandCells = 2;

    // Минимальный перепад высот между истоком и устьем в шагах рельефа:
    // река должна именно стекать с горы, а не переливаться через перешеек.
    int minSourceDrop = 2;

    // Истоки берутся из верхней доли суши по высоте (0.85 = топ-15%).
    float sourceHeightQuantile = 0.85f;
    // Минимальное угловое расстояние между истоками (радианы) до ослабления.
    float sourceMinSeparation = 0.30f;

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
