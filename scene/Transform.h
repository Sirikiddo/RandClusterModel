#pragma once
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <QVector4D>

namespace scene {

// Глобальная система координат: праворукая, Y вверх, метры как базовая единица.
// Transform описывает локальное положение относительно выбранной системы координат.
struct Transform {
    QVector3D position{0.0f, 0.0f, 0.0f};
    QQuaternion rotation{};
    QVector3D scale{1.0f, 1.0f, 1.0f};

    QMatrix4x4 toMatrix() const {
        QMatrix4x4 m;
        m.translate(position);
        m.rotate(rotation);
        m.scale(scale);
        return m;
    }
};

struct CoordinateFrame {
    QVector3D origin{0.0f, 0.0f, 0.0f};
    QQuaternion orientation{};
    QVector3D unitScale{1.0f, 1.0f, 1.0f};

    QMatrix4x4 toMatrix() const {
        QMatrix4x4 m;
        m.translate(origin);
        m.rotate(orientation);
        m.scale(unitScale);
        return m;
    }
};

inline QVector3D localToWorldPoint(const Transform& local, const CoordinateFrame& parent, const QVector3D& point) {
    QMatrix4x4 model = parent.toMatrix() * local.toMatrix();
    return (model * QVector4D(point, 1.0f)).toVector3D();
}

inline QVector3D worldToLocalPoint(const Transform& local, const CoordinateFrame& parent, const QVector3D& worldPoint) {
    QMatrix4x4 model = parent.toMatrix() * local.toMatrix();
    QMatrix4x4 inv = model.inverted();
    return (inv * QVector4D(worldPoint, 1.0f)).toVector3D();
}

inline QVector3D forward(const CoordinateFrame& frame) {
    return frame.orientation.rotatedVector(QVector3D(0.0f, 0.0f, -1.0f));
}

inline QVector3D up(const CoordinateFrame& frame) {
    return frame.orientation.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f));
}

} // namespace scene

