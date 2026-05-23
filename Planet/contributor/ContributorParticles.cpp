#include "contributor/ContributorParticles.h"
#include <random>
#include <cmath>
#include <QtDebug>

namespace {
    float randomFloat(float min, float max, std::mt19937& rng) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }
}

std::vector<ContributorParticle> generateParticleBlob(
    const ContributorParticleBlob& blob,
    std::mt19937& rng)
{
    std::vector<ContributorParticle> particles;
    particles.reserve(blob.particleCount);

    std::uniform_real_distribution<float> distUnit(0.0f, 1.0f);
    std::normal_distribution<float> distNormal(0.0f, 1.0f);

    for (int i = 0; i < blob.particleCount; ++i) {
        ContributorParticle p;

        float typeRand = distUnit(rng);
        bool isSurfaceParticle = (typeRand < 0.7f);

        float radiusFactor;
        float sizeBase;
        float colorVariation;

        if (isSurfaceParticle) {
            radiusFactor = 0.85f + std::abs(distNormal(rng)) * 0.15f;
            radiusFactor = std::min(radiusFactor, 1.02f);
            sizeBase = 0.30f;
            colorVariation = 1.1f;
        }
        else {
            radiusFactor = distUnit(rng) * 0.55f;
            sizeBase = 0.17f;
            colorVariation = 0.85f;
        }

        float theta = distUnit(rng) * 2.0f * 3.14159f;
        float phi = std::acos(2.0f * distUnit(rng) - 1.0f);

        float x = std::sin(phi) * std::cos(theta);
        float y = std::cos(phi);
        float z = std::sin(phi) * std::sin(theta);

        QVector3D localPos(
            x * blob.radius * radiusFactor,
            y * blob.radius * radiusFactor,
            z * blob.radius * radiusFactor
        );

        p.restPosition = blob.center + localPos;
        p.position = p.restPosition;
        p.velocity = QVector3D(0, 0, 0);

        // Вычисляем нормаль: от центра блоба к частице
        QVector3D toParticle = localPos;
        float dist = toParticle.length();
        if (dist > 0.001f) {
            p.normal = toParticle / dist;
        }
        else {
            p.normal = QVector3D(0, 1, 0);
        }

        float rVar = distNormal(rng) * 0.04f;
        float gVar = distNormal(rng) * 0.06f;
        float bVar = distNormal(rng) * 0.04f;

        p.color = QVector3D(
            std::clamp(blob.color.x() + rVar, 0.12f, 0.35f),
            std::clamp(blob.color.y() + gVar, 0.55f, 0.85f),
            std::clamp(blob.color.z() + bVar, 0.12f, 0.35f)
        );

        if (isSurfaceParticle) {
            p.color = p.color * 1.3f;
        }
        else {
            p.color = p.color * 0.5f;
        }

        float sizeRand = 0.6f + distUnit(rng) * 0.8f;
        p.size = sizeBase * sizeRand;

        if (isSurfaceParticle) {
            p.windWeight = 0.8f + distUnit(rng) * 0.7f;
        }
        else {
            p.windWeight = 0.3f + distUnit(rng) * 0.3f;
        }

        p.phase = distUnit(rng) * 6.28318f;

        p.rotation = distUnit(rng) * 6.28318f;

        particles.push_back(p);
    }

    return particles;
}

void debugPrintParticleCount(const std::vector<ContributorParticle>& particles) {
    qDebug() << "Generated" << particles.size() << "particles";
}
