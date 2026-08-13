#pragma once

#include <algorithm>
#include <cmath>
#include <QVector3D>
#include <functional>
#include <vector>

namespace ecs {

    struct Animation {
        enum class Type {
            None,
            MoveTo,
            Bounce,
            Rotate,
            Scale
        };

        Type type = Type::None;
        float duration = 1.0f;
        float elapsed = 0.0f;

        QVector3D startPos;
        QVector3D targetPos;
        int startCell = -1;
        int targetCell = -1;

        // Единичный касательный вектор «вперёд» на поверхности сферы.
        // Он ортогонален радиусу и задаёт горизонтальный курс без крена.
        QVector3D surfaceForward = QVector3D(0, 0, 0);

        std::vector<QVector3D> pathPoints;
        std::vector<float> pathCumulative;
        float pathTotalLength = 0.0f;

        float bounceHeight = 0.1f;
        float arcPeakT = 0.5f;
        bool softLanding = false;

        float rotationSpeed = 180.0f;
        bool completedFired = false;

        // Сохраняем завершённую MoveTo-анимацию ещё на один кадр,
        // чтобы renderer успел показать финальный surfaceForward.
        int finishHoldFrames = 0;
        std::function<void(int entityId)> onComplete;

        bool isFinished() const { return elapsed >= duration; }
        float progress() const {
            return duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
        }

        static float easeOutCubic(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return 1.0f - std::pow(1.0f - t, 3.0f);
        }

        static float easeInOutQuad(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
        }
    };

} // namespace ecs
