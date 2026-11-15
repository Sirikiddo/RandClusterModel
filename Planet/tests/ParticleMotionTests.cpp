#include "tests/ParticleMotionTests.h"
#include "contributor/ContributorParticles.h"
#include <algorithm>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
ContributorParticle makeParticle(float scale, const QQuaternion& orientation, const QVector3D& origin) {
    ContributorParticle p;
    p.motionRestPosition = QVector3D(0.7f, 3.8f, -0.4f);
    p.motionScale = scale;
    p.motionRotation = orientation;
    p.restPosition = origin + orientation.rotatedVector(p.motionRestPosition * scale);
    p.position = p.restPosition;
    p.phase = 0.4f;
    p.windWeight = 0.6f;
    return p;
}
}

void runParticleMotionTests() {
    const ContributorWindField wind;
    for (float dt : {1.0f / 30.0f, 1.0f / 60.0f}) {
        for (float scale : {1.0f, 0.12f, 0.018f, 0.00375f}) {
            // Equator, poles, and tree yaw must not change relative deflection.
            for (float angle : {0.0f, 90.0f, 180.0f}) {
                const auto rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 1).normalized(), angle);
                auto reference = makeParticle(1.0f, QQuaternion(), QVector3D());
                auto transformed = makeParticle(scale, rotation, QVector3D(0.5f, -0.7f, 0.3f));
                float peak = 0.0f;
                for (int frame = 0; frame < 600; ++frame) {
                    advanceContributorParticle(reference, wind, frame * dt, dt);
                    advanceContributorParticle(transformed, wind, frame * dt, dt);
                    const auto expected = reference.position - reference.restPosition;
                    const auto actual = rotation.conjugated().rotatedVector(
                        transformed.position - transformed.restPosition) / scale;
                    require((expected - actual).length() < 0.001f,
                        "Foliage motion changed relative amplitude with tree scale or orientation");
                    peak = std::max(peak, expected.length());
                }
                require(peak > 0.001f, "Motion comparison did not exercise wind");
                const auto position = transformed.position;
                const auto velocity = transformed.velocity;
                advanceContributorParticle(transformed, wind, 1.0f, 0.0f);
                require(transformed.position == position && transformed.velocity == velocity,
                    "A zero-duration frame changed foliage motion");
            }
        }
    }
}
