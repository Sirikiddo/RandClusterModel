#pragma once

#include <QString>
#include <unordered_map>

enum class ResourceType : uint8_t {
    Iron = 0,
    Copper = 1,
    Gold = 2,
    Diamond = 3
};

struct PlayerResources {
    // Количество каждого типа руды
    std::unordered_map<ResourceType, int> resources;

    // Конструктор с начальными значениями
    PlayerResources() {
        resources[ResourceType::Iron] = 0;
        resources[ResourceType::Copper] = 0;
        resources[ResourceType::Gold] = 0;
        resources[ResourceType::Diamond] = 0;
    }

    // Добавить ресурс
    void addResource(ResourceType type, int amount) {
        resources[type] += amount;
    }

    // Получить количество ресурса
    int getResource(ResourceType type) const {
        auto it = resources.find(type);
        return it != resources.end() ? it->second : 0;
    }

    // Проверить, есть ли ресурс в нужном количестве
    bool hasResource(ResourceType type, int amount) const {
        return getResource(type) >= amount;
    }

    // Потратить ресурс
    bool spendResource(ResourceType type, int amount) {
        if (!hasResource(type, amount)) return false;
        resources[type] -= amount;
        return true;
    }

    // Получить строковое представление
    QString toString() const {
        return QString("Iron: %1, Copper: %2, Gold: %3, Diamond: %4")
            .arg(getResource(ResourceType::Iron))
            .arg(getResource(ResourceType::Copper))
            .arg(getResource(ResourceType::Gold))
            .arg(getResource(ResourceType::Diamond));
    }
};

// Вспомогательная функция для конвертации OreType в ResourceType
inline ResourceType oreTypeToResourceType(OreType oreType) {
    switch (oreType) {
    case OreType::Iron: return ResourceType::Iron;
    case OreType::Copper: return ResourceType::Copper;
    case OreType::Gold: return ResourceType::Gold;
    case OreType::Diamond: return ResourceType::Diamond;
    default: return ResourceType::Iron;
    }
}

// Вспомогательная функция для получения названия ресурса
inline QString resourceTypeName(ResourceType type) {
    switch (type) {
    case ResourceType::Iron: return "Iron";
    case ResourceType::Copper: return "Copper";
    case ResourceType::Gold: return "Gold";
    case ResourceType::Diamond: return "Diamond";
    default: return "Unknown";
    }
}