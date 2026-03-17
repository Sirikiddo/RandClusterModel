#pragma once

#include <cmath>
#include <QVector3D>
#include <functional>
#include <vector>

namespace ecs {

    struct Animation {
        enum class Type {
            None,
            MoveTo,           // Перемещение к цели
            Bounce,           // Подпрыгивание
            Rotate,           // Вращение
            Scale             // Масштабирование
        };

        Type type = Type::None;
        float duration = 1.0f;      // Длительность анимации в секундах
        float elapsed = 0.0f;        // Прошедшее время

        // Для перемещения
        QVector3D startPos;
        QVector3D targetPos;
        int startCell = -1;
        int targetCell = -1;

        // Путь для перемещения (если задан, движение идёт по нему)
        std::vector<QVector3D> pathPoints;
        std::vector<float> pathCumulative; // cumulative[i] = длина до точки i
        float pathTotalLength = 0.0f;

        // Для прыжка
        float bounceHeight = 0.1f;

        // Для вращения
        float rotationSpeed = 180.0f; // градусов в секунду

        // Callback по завершению
        std::function<void(int entityId)> onComplete;

        bool isFinished() const { return elapsed >= duration; }
        float progress() const { return duration > 0 ? elapsed / duration : 1.0f; }

        // Интерполяция с easing-функциями
        static float easeOutCubic(float t) {
            return 1.0f - std::pow(1.0f - t, 3.0f);
        }

        static float easeInOutQuad(float t) {
            return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
        }

        static float easeOutBounce(float t) {
            if (t < 1.0f / 2.75f) {
                return 7.5625f * t * t;
            }
            else if (t < 2.0f / 2.75f) {
                t -= 1.5f / 2.75f;
                return 7.5625f * t * t + 0.75f;
            }
            else if (t < 2.5f / 2.75f) {
                t -= 2.25f / 2.75f;
                return 7.5625f * t * t + 0.9375f;
            }
            else {
                t -= 2.625f / 2.75f;
                return 7.5625f * t * t + 0.984375f;
            }
        }
    };

} // namespace ecs