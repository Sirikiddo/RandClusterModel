#include "generation/PerlinNoise.h"

Perlin3D::Perlin3D(unsigned seed) {
    // простейший детерминированный псевдорандом
    for (int i = 0; i < 256; i++) perm[i] = i;
    unsigned s = seed;
    auto rand = [&]() { s = s * 1664525u + 1013904223u; return s & 255u; };
    for (int i = 255; i > 0; i--) std::swap(perm[i], perm[rand()]);
    for (int i = 0; i < 256; i++) perm[256 + i] = perm[i];
}

double Perlin3D::fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double Perlin3D::lerp(double a, double b, double t) {
    return a + t * (b - a);
}

double Perlin3D::grad(int h, double x, double y, double z) {
    int g = h & 15;
    double u = g < 8 ? x : y;
    double v = g < 4 ? y : (g == 12 || g == 14 ? x : z);
    return ((g & 1) ? -u : u) + ((g & 2) ? -v : v);
}

double Perlin3D::noise(double x, double y, double z) const {
    int X = (int)floor(x) & 255;
    int Y = (int)floor(y) & 255;
    int Z = (int)floor(z) & 255;
    x -= floor(x); y -= floor(y); z -= floor(z);
    double u = fade(x), v = fade(y), w = fade(z);
    int A = perm[X] + Y, AA = perm[A] + Z, AB = perm[A + 1] + Z;
    int B = perm[X + 1] + Y, BA = perm[B] + Z, BB = perm[B + 1] + Z;

    return lerp(
        lerp(
            lerp(grad(perm[AA], x, y, z),
                grad(perm[BA], x - 1, y, z), u),
            lerp(grad(perm[AB], x, y - 1, z),
                grad(perm[BB], x - 1, y - 1, z), u),
            v),
        lerp(
            lerp(grad(perm[AA + 1], x, y, z - 1),
                grad(perm[BA + 1], x - 1, y, z - 1), u),
            lerp(grad(perm[AB + 1], x, y - 1, z - 1),
                grad(perm[BB + 1], x - 1, y - 1, z - 1), u),
            v),
        w);
}