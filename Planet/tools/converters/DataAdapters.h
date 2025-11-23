#pragma once
#include <QVector3D>
#include <functional>
#include <vector>

namespace converters {

struct HeightSample {
    float normalizedHeight = 0.0f; // -1..1
};

class HeightmapAdapter {
public:
    static int toDiscreteHeight(const HeightSample& sample, float scale, float seaLevel);
};

class MaterialAdapter {
public:
    static QString pickMaterial(float height, float seaLevel);
};

class GeometryAdapter {
public:
    template <class Container>
    static std::vector<QVector3D> convert(const Container& data, const std::function<QVector3D(const typename Container::value_type&)>& mapper) {
        std::vector<QVector3D> result;
        result.reserve(data.size());
        for (const auto& item : data) {
            result.push_back(mapper(item));
        }
        return result;
    }
};

} // namespace converters

