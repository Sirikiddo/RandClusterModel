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

    std::vector<QVector3D> pathPoints;
    std::vector<float> pathCumulative;
    float pathTotalLength = 0.0f;

    float bounceHeight = 0.1f;
    float arcPeakT = 0.5f;
    bool softLanding = false;

    float rotationSpeed = 180.0f;
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

    static float easeOutBounce(float t) {
        if (t < 1.0f / 2.75f) {
            return 7.5625f * t * t;
        }
        if (t < 2.0f / 2.75f) {
            t -= 1.5f / 2.75f;
            return 7.5625f * t * t + 0.75f;
        }
        if (t < 2.5f / 2.75f) {
            t -= 2.25f / 2.75f;
            return 7.5625f * t * t + 0.9375f;
        }
        t -= 2.625f / 2.75f;
        return 7.5625f * t * t + 0.984375f;
    }
};

} // namespace ecs
