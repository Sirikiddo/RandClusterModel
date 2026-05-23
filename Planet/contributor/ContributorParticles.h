#pragma once

#include <QVector3D>
#include <vector>
#include <random>

// Одна частица для кроны дерева
struct ContributorParticle {
    QVector3D restPosition{ 0, 0, 0 };
    QVector3D position{ 0, 0, 0 };
    QVector3D velocity{ 0, 0, 0 };
    QVector3D color{ 0.2f, 0.7f, 0.2f };
    QVector3D normal{ 0, 1, 0 };  // <-- добавили
    float size = 0.05f;
    float windWeight = 1.0f;
    float phase = 0.0f;
    float rotation = 0.0f;
};

// Набор частиц для одного "облака" листвы
struct ContributorParticleBlob {
    QVector3D center{ 0, 0, 0 };
    float radius = 0.5f;
    int particleCount = 50;
    QVector3D color{ 0.2f, 0.7f, 0.2f };
};

// Ветровое поле
struct ContributorWindField {
    QVector3D direction = QVector3D(1.0f, 0.0f, 0.0f);
    float strength = 0.25f;
    float gustStrength = 0.35f;
    float gustSpeed = 1.5f;
    float turbulence = 0.15f;
};

// Объявления функций (без inline здесь)
std::vector<ContributorParticle> generateParticleBlob(
    const ContributorParticleBlob& blob,
    std::mt19937& rng);

void debugPrintParticleCount(const std::vector<ContributorParticle>& particles);