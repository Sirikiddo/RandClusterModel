---
tags: [gamenew, terrain, water, culling, mesh]
---

# Меш рельефа, вода и culling

## Terrain mesh

`TerrainMeshGenerator` конфигурирует `TerrainTessellator`. Выход — SoA arrays `pos`, `col`, `norm`, `idx` и `triOwner`.

Tessellator для каждой polygon cell готовит:

- inner cap/fan;
- blades вдоль рёбер;
- corner triangles;
- post-pass cliffs между сторонами общей edge;
- lift радиуса на `height × heightStep`;
- цвет биома с температурной поправкой и потенциальной ore grain overlay.

`smoothMaxDelta=1` классифицирует одноуровневый перепад как slope; более крупный — cliff. При выключенном smooth даже перепад 1 не сглаживается.

## Вода

`WaterMeshGenerator` создаёт geometry только для `Biome::Sea`. Каждый polygon разбивается fan-ом от centroid; triangle рекурсивно subdivide-ится 3 раза и все точки проецируются на radius 1.0. Отдельный attribute `edgeFlags` помогает shader различать края.

Water shader имеет procedural waves/noise, lighting, alpha и environment cubemap. `WaterRenderer` включает blending и запрещает depth write на время прохода.

> [!bug] Фактическая анимация воды
> `HexSphereWidget` создаёт `waterTimer_` и подключает timeout к `advanceWaterTime`, но нигде не вызывает `waterTimer_->start()`. Поэтому `uTime` в обычном runtime остаётся 0, несмотря на готовый animated shader.

## Wire и outline

- Wire mesh — линии unique dual edges на unit sphere; renderer рисует его после terrain/water/entities.
- Selection outline строит каждую polygon edge на радиусе cell height + bias. При smooth и соседнем перепаде 1 линия использует среднюю высоту.
- Build preview использует тот же outline generator, но для соседних свободных клеток Explorer вместо выбранных клеток.

## Culling — активный путь

`HexSphereRenderer::updateVisibility(cameraPos)` передаёт camera в scene и периодически вызывает `getVisibleIndices`. Scene cache хранит center каждой terrain triangle и выбирает triangles, чья radial normal направлена к камере (`dot > threshold`). Затем renderer обновляет только terrain IBO через `glBufferSubData`.

Частота адаптивна к оценке скорости camera, времени с прошлого update и distance. Vertex/color/normal buffers не фильтруются.

## Альтернативный TerrainCulling

Класс `culling/TerrainCulling` реализует похожий cache/filter и возвращает отдельный culled mesh. Он компилируется, но active renderer его не создаёт и не вызывает. Это дублирующая экспериментальная реализация.

## Upload strategy

При L < 4 terrain/wire заявлены `GL_STATIC_DRAW`, при L ≥4 — `GL_DYNAMIC_DRAW`. Но terrain index buffer внутри `uploadTerrainInternal` всегда создаётся с `GL_DYNAMIC_DRAW`, потому что culling меняет indices.
