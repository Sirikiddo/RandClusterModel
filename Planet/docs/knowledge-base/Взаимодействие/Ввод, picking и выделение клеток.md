---
tags: [gamenew, input, picking, selection]
---

# Ввод, picking и выделение клеток

## Маршрутизация событий

Qt event handlers `HexSphereWidget` ничего не вычисляют: передают событие `InputController`, затем `applyResponse` обновляет HUD и планирует repaint.

## Camera

- ПКМ включает `rotating_`; mouse delta создаёт X/Y quaternions и умножает текущую rotation.
- View сначала `lookAt(eye=(0,0,distance), center=0)`, затем применяет sphere rotation.
- Wheel масштабирует distance на `0.9^steps` и clamp 1.2…10.
- Screen ray строится через inverse `(projection × view)` по near/far NDC.

## Picking terrain

Активный `pickTerrainAt` проходит по triangles текущего tessellated terrain, использует Möller–Trumbore, выбирает минимальный positive `t`, а cell id берёт из `triOwner`. Это height-aware picking: клики соответствуют реальному lifted/cliff mesh.

`pickCellAt` по старым normalized `model.pickTris()` существует отдельно, но основной `pickSceneAt` его не использует.

## Picking entities

Для каждой пары `Collider + Transform` выполняется ray/sphere intersection. `pickSceneAt` сравнивает ближайший entity hit и terrain hit. Collider — приближённая сфера, не triangle mesh модели.

## Логика ЛКМ

```mermaid
flowchart TD
  C["ЛКМ"] --> H["pickSceneAt"]
  H --> PM{"Placement mode?"}
  PM -- Delete --> DEL["Удалить factory/mine"]
  PM -- Build --> OCC{"Entity/occupied/allowed?"}
  OCC -- yes --> MSG["HUD отказ"]
  OCC -- no --> BUILD["Создать ECS building"]
  PM -- No --> EH{"Hit entity?"}
  EH -- yes --> SEL["selectEntity / toggle"]
  EH -- no --> ES{"Entity already selected?"}
  ES -- yes --> MOVE["Путь и движение на cell"]
  ES -- no --> CELL["toggleCellSelection + upload outline"]
```

## Выделение клетки

`selectedCells_` — `QSet<int>`. Toggle меняет set и dirty flag. `uploadSelection()`:

1. формирует `SceneDagRequest` со snapshot, sorted selection, visual params и ECS model requests;
2. получает DAG outline/trees;
3. сохраняет outline cache в scene;
4. загружает GL_LINES VBO.

Если active building mode, визуализируется не selection set, а `buildPreviewCells_`. DAG всё равно пересчитывается по обычному selection, после чего preview outline строится прямым legacy generator.

## Команды над клетками

`+/-` и biomes 1…8 мутируют все выбранные клетки, затем выполняют полный rebuild derived geometry и upload. `P` требует ровно две клетки. `Clear Selection` очищает клетки, но команда `C` очищает только path VBO.
