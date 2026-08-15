---
tags: [gamenew, build, tests, benchmark]
---

# Сборка, запуск, тесты и benchmark

## Solution и project

- Solution: `F:\GameNew\Planet\GameNew.sln`.
- Основной актуальный project: `GameNew.vcxproj`, C++20.
- В корне также сохранён `Planet.vcxproj`; перед изменением project metadata проверять, какой project загружен в solution.
- Include paths: project root, `glm`, `include`, `third_party/ProcessDAG/include`.
- Libraries: `glew32`, `glfw3dll`, `opengl32`, `freetype`; UI использует Qt/MSBuild integration.

Готовый debug executable присутствует как `F:\GameNew\Planet\x64\Debug\GameNew.exe`, но документация должна ориентироваться на сборку из source.

## Режимы запуска

- Обычный GUI: без специальных аргументов.
- Benchmark: `--benchmark` в argv/Windows command line или environment `GAME_NEW_BENCHMARK`. Используется `QCoreApplication`, GUI не создаётся.
- Contributor: compile-time `kContributorMode=true` в `core/AppViewConfig.h`, затем rebuild.

## Benchmark

`runDagBackendBenchmark` записывает `dag_backend_benchmark_results.csv` и сравнивает:

- DAG terrain vs Legacy terrain для climate L2, changed seed L2, perlin L3;
- DAG selection для L2/L4: empty, pentagon, hexagon, two cells, repeat, selection/bias/smooth/height changes;
- DAG scene vs direct legacy scene-derived trees/models;
- elapsed time, input bytes, output vertices, executed/skipped nodes, value cache и plan cache.

Compatibility terrain проверяет level, generator, cell count и только первые 32 height/biome. Scene compatibility значительно слабее: она не сравнивает массивы по значениям, а проверяет наличие terrain и факт выполнения/skip DAG nodes. Benchmark — индикатор производительности/регрессии, не доказательство полной эквивалентности.

## Диагностика долгой генерации

Полная генерация пишет строки с префиксом `[Perf][Generation]` в Visual Studio Output/debug console. Для сравнения L3/L4 использовать одинаковые configuration, seed и параметры. Основные стадии: `terrain_backend_total`, `rebuild_topology`, `water_proxy`, `terrain_mesh`, `scene_projection`, `surface_atlas_update`, `renderer_upload_scene`, `upload_buffers` и `set_subdivision_total`.

Для `surface_atlas_update` сопоставлять `atlas_vertices`, `unique_directions`, `cache_hits`, `shoreline_arcs`, `bvh_nodes`, `bvh_node_visits`, `exact_distance_tests`, `brute_force_tests`, `index_build_ms`, `distance_query_ms` и `workers`. Совместимые aliases `distance_tests`/`distance_field_ms` теперь отражают фактические exact tests/query time, а не старое полное произведение. Если велик `projection_ms`, смотреть вложенные topology/mesh стадии. `terrain_gpu_upload_ms` и `water_gpu_upload_ms` — CPU wall time вызова upload, а не чистое GPU execution time.

## Тесты

- `tests/scene_integration.cpp` — lifecycle маленького ECS, script update, selection, destroy.
- `tests/dag_backend_benchmark.cpp` — создаёт CSV, проверяет report ok и наличие строк DAG/legacy/statistics.
- `tests/SelectionOutlineTests.cpp` — лёгкий input, direct/DAG equivalence, codec, L2/L4 size и selective execution; запускается через `--selection-tests`.
- `tests/SurfaceAtlasDistanceTests.cpp` — spherical BVH против brute-force oracle, signed output, deduplication, serial/parallel equality и structural L2–L4 metrics; запускается через `--surface-atlas-tests`.
- `--water-tests` и `--climate-tests` запускают существующие CPU unit tests.
- `tools/model_cache_test.cpp` — отдельный model cache tool.

Тесты не покрывают OpenGL rendering, picking, реальное движение машины, tree placement equality или serialization edge cases.

## Изменение source/assets

Проект перечисляет `.cpp/.h` явно. При добавлении файла обновить `.vcxproj` и `.vcxproj.filters`. Assets живут в `resources`; многие пути runtime относительные, поэтому current working directory должен позволять найти `resources/...`.
