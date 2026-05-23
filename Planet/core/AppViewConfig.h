#pragma once

#include <cstdint>

enum class SceneViewMode : uint8_t {
    Planet = 0,
    Contributor = 1
};

struct AppViewConfig {
    SceneViewMode sceneViewMode = SceneViewMode::Planet;

    constexpr bool isContributorMode() const {
        return sceneViewMode == SceneViewMode::Contributor;
    }
};

// Flip this flag to switch the app into the single-tree contributor view.
inline constexpr bool kContributorMode = false;

constexpr AppViewConfig defaultAppViewConfig() {
    return AppViewConfig{
        kContributorMode ? SceneViewMode::Contributor : SceneViewMode::Planet
    };
}
