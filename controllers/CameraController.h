#pragma once

#include <QMatrix4x4>
#include <QPoint>
#include <QQuaternion>
#include <QVector3D>

class CameraController {
public:
    CameraController();

    void resize(int width, int height, float devicePixelRatio);
    void reset();

    void rotate(const QPoint& delta);
    void zoom(float wheelSteps);

    const QMatrix4x4& view() const { return view_; }
    const QMatrix4x4& projection() const { return proj_; }

    QVector3D rayOrigin() const;
    QVector3D rayDirectionFromScreen(int sx, int sy, int width, int height, float devicePixelRatio) const;

private:
    void updateView();

    QMatrix4x4 view_{};
    QMatrix4x4 proj_{};

    float distance_ = 2.2f;
    QQuaternion sphereRotation_{};
};
