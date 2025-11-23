#include "controllers/CameraController.h"

#include <QMatrix4x4>
#include <QVector4D>
#include <algorithm>
#include <cmath>

CameraController::CameraController() {
    updateView();
}

void CameraController::resize(int width, int height, float devicePixelRatio) {
    const int pw = int(width * devicePixelRatio);
    const int ph = int(height * devicePixelRatio);
    proj_.setToIdentity();
    proj_.perspective(50.0f, float(pw) / float(std::max(ph, 1)), 0.01f, 50.0f);
}

void CameraController::reset() {
    distance_ = 2.2f;
    sphereRotation_ = QQuaternion();
    updateView();
}

void CameraController::rotate(const QPoint& delta) {
    const float sensitivity = 0.002f;
    QQuaternion rotationX = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), delta.x() * sensitivity * 180.0f);
    QQuaternion rotationY = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), delta.y() * sensitivity * 180.0f);
    QQuaternion rotation = rotationY * rotationX;
    sphereRotation_ = rotation * sphereRotation_;
    updateView();
}

void CameraController::zoom(float wheelSteps) {
    distance_ *= std::pow(0.9f, wheelSteps);
    distance_ = std::clamp(distance_, 1.2f, 10.0f);
    updateView();
}

QVector3D CameraController::rayOrigin() const {
    const QMatrix4x4 invView = view_.inverted();
    return (invView.map(QVector4D(0, 0, 0, 1))).toVector3D();
}

QVector3D CameraController::rayDirectionFromScreen(int sx, int sy, int width, int height, float devicePixelRatio) const {
    const float dpr = devicePixelRatio;
    const float w = float(width * dpr);
    const float h = float(height * dpr);
    const float x = 2.0f * (float(sx) * dpr / w) - 1.0f;
    const float y = 1.0f - 2.0f * (float(sy) * dpr / h);
    const QMatrix4x4 inv = (proj_ * view_).inverted();
    QVector4D pNear = inv.map(QVector4D(x, y, -1.0f, 1.0f));
    QVector4D pFar = inv.map(QVector4D(x, y, 1.0f, 1.0f));
    pNear /= pNear.w();
    pFar /= pFar.w();
    return (pFar.toVector3D() - pNear.toVector3D()).normalized();
}

void CameraController::updateView() {
    view_.setToIdentity();
    const QVector3D eye = QVector3D(0, 0, distance_);
    const QVector3D center = QVector3D(0, 0, 0);
    const QVector3D up = QVector3D(0, 1, 0);
    view_.lookAt(eye, center, up);
    view_.rotate(sphereRotation_);
}
