#pragma once

#include <QVector3D>
#include <vector>

namespace ecs {

    struct PathFollower {
        std::vector<QVector3D> path;          // Траектория движения
        size_t currentSegment = 0;            // Текущий сегмент пути
        float segmentProgress = 0.0f;         // Прогресс внутри сегмента (0-1)
        float speed = 2.0f;                   // Скорость движения (единиц в секунду)
        bool isMoving = false;                // Флаг активности движения

        // Сброс состояния
        void reset() {
            path.clear();
            currentSegment = 0;
            segmentProgress = 0.0f;
            isMoving = false;
        }

        // Установка нового пути
        void setPath(const std::vector<QVector3D>& newPath) {
            path = newPath;
            currentSegment = 0;
            segmentProgress = 0.0f;
            isMoving = !path.empty();
        }

        // Получение текущей целевой позиции
        QVector3D getCurrentTarget() const {
            if (path.empty() || currentSegment >= path.size() - 1) {
                return {};
            }
            return path[currentSegment + 1];
        }
    };

} // namespace ecs