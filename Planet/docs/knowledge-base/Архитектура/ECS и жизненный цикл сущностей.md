---
tags: [gamenew, ecs, entities]
---

# ECS и жизненный цикл сущностей

## Хранилище

`ComponentStorage` выдаёт последовательные integer id, хранит entity order и отдельные `unordered_map` компонентов. `emplace<T>`, `get<T>`, `each<...>` реализуют небольшой типизированный ECS без system scheduler.

## Сущности Planet mode

- Explorer создаётся при OpenGL initialization, получает `Mesh="car"`, `Transform`, sphere `Collider(radius=0.20)` и стартовую клетку с максимальным `centroid.z`.
- Factory/Mine создаются из placement mode, получают соответствующий mesh id, transform, collider `0.16` и выбранную клетку.
- Деревья не ECS entities: это `TreePlacement` в scene controller.
- Terrain и water также не ECS entities, а отдельные CPU/GPU ресурсы.

## Selection

`ComponentStorage::setSelected(id, true)` снимает selection со всех остальных сущностей. Cell selection хранится независимо в `QSet<int> selectedCells_`; поэтому можно одновременно иметь выделенные клетки и выбранную сущность, хотя interaction flow часто очищает/перестраивает клетки при движении.

## Update

`ComponentStorage::update(dt)`:

1. вызывает callbacks `Script::onUpdate`;
2. обновляет все `Animation`;
3. выполняет completion callback;
4. держит завершённую MoveTo ещё один update, чтобы renderer увидел финальный forward;
5. удаляет завершённый animation component.

`Rotate`, `Scale`, `Bounce` перечислены в enum, но update реализует только `MoveTo`; bounce является параметром MoveTo arc, а не отдельным активным типом.

## Координаты

Глобальная система праворукая, Y-up. На сфере локальный up — нормализованный radial vector. `computeSurfacePoint` возвращает `centroid * (1 + height * heightStep + offset)`. `Transform::surfaceForward` хранит касательное направление машины между animation components.
