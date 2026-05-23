#include "TerrainVisibility.h"

#include <algorithm>

std::vector<uint32_t> buildVisibleTerrainIndices(const TerrainMesh& mesh, const QVector3D& cameraPos) {
    static constexpr float kVisibilityDotMargin = -0.05f;

    const QVector3D toCamVector = cameraPos;
    if (toCamVector.lengthSquared() <= 1.0e-8f) {
        return mesh.idx;
    }

    const QVector3D toCam = toCamVector.normalized();
    std::vector<uint32_t> visible;
    visible.reserve(mesh.idx.size() / 2);

    for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) {
        const auto vertexAt = [&mesh](uint32_t index) {
            const size_t base = static_cast<size_t>(index) * 3;
            return QVector3D(mesh.pos[base], mesh.pos[base + 1], mesh.pos[base + 2]);
        };

        const uint32_t i0 = mesh.idx[i];
        const uint32_t i1 = mesh.idx[i + 1];
        const uint32_t i2 = mesh.idx[i + 2];
        const QVector3D center = (vertexAt(i0) + vertexAt(i1) + vertexAt(i2)) * (1.0f / 3.0f);
        if (QVector3D::dotProduct(center.normalized(), toCam) > kVisibilityDotMargin) {
            visible.push_back(i0);
            visible.push_back(i1);
            visible.push_back(i2);
        }
    }

    return visible;
}

bool TerrainVisibilityController::shouldUpdate(const QVector3D& cameraPos) {
    if (!speedTimerStarted_) {
        speedTimer_.start();
        speedTimerStarted_ = true;
        return true;
    }

    const float dt = speedTimer_.elapsed() / 1000.0f;
    if (dt > 0.1f) {
        const QVector3D newVelocity = (cameraPos - lastCameraPos_) / dt;
        velocity_ = velocity_ * 0.7f + newVelocity * 0.3f;
        speedTimer_.restart();
    }

    if (!hasCameraMoved(cameraPos, config_.baseThreshold)) {
        return false;
    }

    const float speed = velocity_.length();
    if (!lastUpdateStarted_) {
        return true;
    }

    const float timeSinceLastUpdate = lastUpdateTimer_.elapsed() / 1000.0f;
    bool needUpdate = false;
    if (speed > config_.fastSpeed) {
        needUpdate = timeSinceLastUpdate > config_.fastUpdateInterval;
    }
    else if (speed > config_.mediumSpeed) {
        needUpdate = timeSinceLastUpdate > config_.mediumUpdateInterval;
    }
    else {
        needUpdate = timeSinceLastUpdate > config_.slowUpdateInterval;
    }

    if ((cameraPos - lastCameraPos_).length() > config_.forceUpdateDistance) {
        needUpdate = true;
    }

    return needUpdate;
}

void TerrainVisibilityController::markVisibilityApplied(const QVector3D& cameraPos) {
    lastCameraPos_ = cameraPos;
    if (!lastUpdateStarted_) {
        lastUpdateTimer_.start();
        lastUpdateStarted_ = true;
    }
    else {
        lastUpdateTimer_.restart();
    }
}

void TerrainVisibilityController::reset() {
    lastCameraPos_ = QVector3D(0.0f, 0.0f, 5.0f);
    velocity_ = QVector3D(0.0f, 0.0f, 0.0f);
    speedTimerStarted_ = false;
    lastUpdateStarted_ = false;
}

bool TerrainVisibilityController::hasCameraMoved(const QVector3D& cameraPos, float distanceThreshold) const {
    float adaptiveThreshold = distanceThreshold;
    const float cameraDistance = cameraPos.length();
    if (cameraDistance > config_.farDistance) {
        adaptiveThreshold *= (cameraDistance / config_.farDistance);
    }
    else if (cameraDistance < config_.nearDistance) {
        adaptiveThreshold *= (cameraDistance / config_.nearDistance);
    }

    adaptiveThreshold = std::max(adaptiveThreshold, 0.05f);
    const float thresholdSq = adaptiveThreshold * adaptiveThreshold;
    return (cameraPos - lastCameraPos_).lengthSquared() > thresholdSq;
}
