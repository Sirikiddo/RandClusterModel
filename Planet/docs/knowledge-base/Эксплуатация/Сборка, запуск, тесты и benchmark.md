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
- DAG scene vs direct legacy scene-derived operations;
- baseline/repeat/selection/visual/terrain change/revert;
- executed/skipped nodes, value cache и plan cache.

Compatibility terrain проверяет level, generator, cell count и только первые 32 height/biome. Scene compatibility значительно слабее: она не сравнивает массивы по значениям, а проверяет наличие terrain и факт выполнения/skip DAG nodes. Benchmark — индикатор производительности/регрессии, не доказательство полной эквивалентности.

## Тесты

- `tests/scene_integration.cpp` — lifecycle маленького ECS, script update, selection, destroy.
- `tests/dag_backend_benchmark.cpp` — создаёт CSV, проверяет report ok и наличие строк DAG/legacy/statistics.
- `tools/model_cache_test.cpp` — отдельный model cache tool.

Тесты не покрывают OpenGL rendering, picking, реальное движение машины, tree placement equality или serialization edge cases.

## Изменение source/assets

Проект перечисляет `.cpp/.h` явно. При добавлении файла обновить `.vcxproj` и `.vcxproj.filters`. Assets живут в `resources`; многие пути runtime относительные, поэтому current working directory должен позволять найти `resources/...`.
