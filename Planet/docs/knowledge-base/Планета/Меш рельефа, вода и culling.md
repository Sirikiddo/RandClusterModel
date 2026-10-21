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

`WaterRenderer` рисует один экранный треугольник из трёх вершин, вычисляемых через `gl_VertexID`. Сетка воды на CPU, её revision и загрузка vertex/index buffers больше не используются. `WaterMeshGenerator` оставлен для исторического проекта GameNew и сравнительного `--water-screen-benchmark`; обычный рендеринг Planet его не вызывает.

Fragment shader строит луч из камеры и ищет первое пересечение с аналитической поверхностью внутри прежних границ wave shell. Средний уровень моря не ограничивает покрытие: впадины и гребни за его горизонтом остаются доступны поиску. Sea/Land и берег определяются по атласам поверхности; scene depth задаёт перекрытие рельефом и оптическую толщину. Шейдер записывает глубину найденной точки через `gl_FragDepth`. Проход использует premultiplied blending, depth test и depth write.

Замена устраняет генерацию, хранение, загрузку и обработку миллионов треугольников покрытия; стоимость поиска волн по пикселям и построения атласов остаётся. Дополнительные ранние отсечения не вводятся. Флажок `Waves` в `Water settings` отдельно включает упрощённый режим: при выключении используется прямое пересечение со сферой среднего уровня моря и радиальная нормаль, без волн, ряби, отражений, бликов и пены. Глубина, берег, прозрачность и простое освещение сохраняются. Значения волновых настроек сохраняются для обратного включения.

Проверки: `--water-tests` для CPU-модели и `--water-screen-tests` для компиляции GLSL и сравнения экранного треугольника с двухтреугольным прямоугольником по цвету и глубине на GPU, без MSAA и с MSAA 4x. Последний тест использует синтетическое море; реальные берега проверяются в приложении.

## Wire и outline

- Отладочная красная сетка исходных ячеек удалена из прохода Planet: её вершины не генерируются, GPU-буферы не создаются. Линейный шейдер остаётся общим ресурсом для пути; контуры выделения рисуются отдельно.
- Selection outline строит каждую polygon edge на радиусе cell height + bias. При smooth и соседнем перепаде 1 линия использует среднюю высоту.
- Build preview использует тот же outline generator, но для соседних свободных клеток Explorer вместо выбранных клеток.

## Culling — активный путь

`HexSphereRenderer::updateVisibility(cameraPos)` передаёт camera в scene и периодически вызывает `getVisibleIndices`. Scene cache хранит center каждой terrain triangle и выбирает triangles, чья radial normal направлена к камере (`dot > threshold`). Затем renderer обновляет только terrain IBO через `glBufferSubData`.

Частота адаптивна к оценке скорости camera, времени с прошлого update и distance. Vertex/color/normal buffers не фильтруются.

## Альтернативный TerrainCulling

Класс `culling/TerrainCulling` реализует похожий cache/filter и возвращает отдельный culled mesh. Он компилируется, но active renderer его не создаёт и не вызывает. Это дублирующая экспериментальная реализация.

## Upload strategy

При L < 4 terrain/wire заявлены `GL_STATIC_DRAW`, при L ≥4 — `GL_DYNAMIC_DRAW`. Но terrain index buffer внутри `uploadTerrainInternal` всегда создаётся с `GL_DYNAMIC_DRAW`, потому что culling меняет indices.
