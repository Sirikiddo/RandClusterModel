#pragma once
#include <cmath>
#include <array>

class Perlin3D {
public:
    Perlin3D(unsigned seed = 2024);
    double noise(double x, double y, double z) const;

private:
    static double fade(double t);
    static double lerp(double a, double b, double t);
    static double grad(int h, double x, double y, double z);

    std::array<int, 512> perm;
};