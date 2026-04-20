#include "DataAdapters.h"
#include <QString>
#include <QtGlobal>

namespace converters {

int HeightmapAdapter::toDiscreteHeight(const HeightSample& sample, float scale, float seaLevel) {
    float height = sample.normalizedHeight * scale;
    return qRound(height + seaLevel);
}

QString MaterialAdapter::pickMaterial(float height, float seaLevel) {
    if (height < seaLevel - 0.1f) return "water";
    if (height < seaLevel + 0.2f) return "sand";
    if (height < seaLevel + 1.0f) return "grass";
    return "rock";
}

} // namespace converters

