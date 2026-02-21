#pragma once

#include <cstdint>

struct DebugOverlay {
    // Версия сцены: растет при обработке команд ядром.
    uint64_t sceneVersion = 0;

    // Есть ли запланированная работа в текущем тике.
    bool hasPlan = false;

    // Флаг занятости асинхронных задач (резерв под будущую интеграцию).
    bool asyncBusy = false;

    // Метрики кадра.
    float dtMs = 0.0f;
    float fps = 0.0f;
};
