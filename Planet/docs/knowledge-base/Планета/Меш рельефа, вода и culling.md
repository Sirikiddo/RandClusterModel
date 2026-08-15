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

`WaterMeshGenerator` создаёт conservative proxy по всем polygon cells, включая land. Каждый polygon разбивается fan-ом от centroid; triangle рекурсивно subdivide-ится 2 раза, то есть даёт 16 triangles, и все точки проецируются на radius 1.0. Semantic Sea/Land rejection выполняется позже через hydrology atlas/shader.

Water shader имеет procedural waves/noise, lighting, alpha и environment cubemap. `WaterRenderer` включает blending и запрещает depth write на время прохода.

> [!warning] Стоимость water proxy
> Разрешение water автоматически растёт вместе с terrain level и дополнительно умножается на 16. На L6 proxy содержит около 3,93 млн triangles, рисуется целиком каждый кадр и не использует terrain visibility. `animationTimer_` каждые ~16 ms вызывает `advanceWaterTime`, поэтому shader time в текущем runtime продвигается.

## Wire и outline

- Wire mesh — линии unique dual edges на unit sphere; renderer рисует его после terrain/water/entities.
- Selection outline строит каждую polygon edge на радиусе cell height + bias. При smooth и соседнем перепаде 1 линия использует среднюю высоту.
- Build preview использует тот же outline generator, но для соседних свободных клеток Explorer вместо выбранных клеток.

## Surface atlas и shore-distance

`PlanetSurfaceAtlasPass::updateMesh()` оставляет top triangles, строит shoreline arcs и записывает в atlas-геометрию signed distance до ближайшего берега. Чистый CPU `SurfaceAtlasMeshBuilder` отделён от GL upload: он кэширует bitwise-одинаковые позиции и ищет ближайшую дугу через точный spherical BVH. Независимые запросы больших наборов выполняются через глобальный Qt thread pool.

Debug baseline L4: 230 400 atlas vertices, 1 212 arcs, 279 244 800 точных distance tests и 165,56 с только на distance field. Полная смена level занимает 168,81 с, то есть atlas отвечает за 98,1% времени. Построение arcs и GPU upload занимают единицы миллисекунд и bottleneck не являются.

После P0 полный Debug runtime L4 сократился с 168,81 до 3,71 с, а atlas — со 165,56 до 0,504 с. На L5 полная генерация занимает 15,01 с, atlas — 2,27 с. В runtime L5 дедупликация оставляет 197 388 unique directions из 921 600 vertices, а BVH выполняет 5 612 837 точных проверок вместо старого потенциального произведения 2 689 228 800 — примерно в 479 раз меньше.

Раньше каждая неиндексированная atlas vertex линейно перебирала все shoreline arcs и повторяла дорогую сферическую геометрию даже для совпадающих позиций соседних triangles. Теперь bitwise cache вычисляет расстояние один раз для unique direction, точный spherical BVH доказуемо отбрасывает группы далёких arcs, а большие наборы запросов распределяются через глобальный Qt thread pool. В leaf используется прежняя точная функция, поэтому ускорение не меняет water shaders, cubemap resolution, волны, пену, освещение или береговое затухание. Brute-force path сохранён только как oracle для `--surface-atlas-tests`. Полные цифры и объяснение: [[Эксплуатация/Аудит производительности высоких subdivision#После P0 полный Debug runtime L2–L5]].

## Culling — активный путь

`HexSphereRenderer::updateVisibility(cameraPos)` передаёт camera в scene и периодически вызывает `getVisibleIndices`. Scene cache хранит center каждой terrain triangle и выбирает triangles, чья radial normal направлена к камере (`dot > 0`). Затем renderer полностью задаёт terrain IBO через `glBufferData`.

Текущий camera update содержит двойной traversal: `hasCameraMoved()` вызывает `getVisibilityStats()`, который уже строит visible indices, а затем `updateVisibility()` вызывает `getVisibleIndices()` второй раз. Первый результат используется только для debug log.

При этом OpenGL 3.3 уже выполняет back-face culling, frustum clipping и depth testing. Требуемый baseline — неизменяемый полный terrain IBO с `GL_CULL_FACE`; если vertex workload L5/L6 останется высоким, целевой гибрид делит планету на крупные chunks и проверяет на CPU только десятки bounds. Детали и ограничения: [[Эксплуатация/Аудит производительности высоких subdivision#3. Использовать culling OpenGL 3.3 и при необходимости крупные terrain chunks]].

Частота адаптивна к оценке скорости camera, времени с прошлого update и distance. Vertex/color/normal buffers не фильтруются.

## Альтернативный TerrainCulling

Класс `culling/TerrainCulling` реализует похожий cache/filter и возвращает отдельный culled mesh. Он компилируется, но active renderer его не создаёт и не вызывает. Это дублирующая экспериментальная реализация.

## Upload strategy

При L < 4 terrain/wire заявлены `GL_STATIC_DRAW`, при L ≥4 — `GL_DYNAMIC_DRAW`. Но terrain index buffer внутри `uploadTerrainInternal` всегда создаётся с `GL_DYNAMIC_DRAW`, потому что culling меняет indices.

Расчёт geometry и варианты исправления описаны в [[Эксплуатация/Аудит производительности высоких subdivision#P0. Устранить полный перебор shoreline arcs для каждой atlas vertex]], [[Эксплуатация/Аудит производительности высоких subdivision#7. Отвязать water resolution от terrain subdivision]] и [[Эксплуатация/Аудит производительности высоких subdivision#8. Переиспользовать vertices terrain mesh и точно управлять allocations]].
