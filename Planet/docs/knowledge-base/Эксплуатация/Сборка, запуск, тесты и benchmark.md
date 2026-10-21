---
tags: [gamenew, build, tests, benchmark]
---

# Сборка, запуск, тесты и benchmark

## Solution и project

- Основной solution: `Planet.sln` в корне репозитория, на уровень выше этого каталога.
- Основной project: `Planet/Planet.vcxproj`, конфигурации Debug/Release x64, MSVC v143 и Qt 6.8.
- `GameNew.sln` и `GameNew.vcxproj` сохранены как историческая альтернативная конфигурация. Новые исходники и ресурсы добавлять в `Planet.vcxproj` и `Planet.vcxproj.filters`.
- Include paths: project root, `glm`, `include`, `third_party/ProcessDAG/include`.
- Libraries: `glew32`, `glfw3dll`, `opengl32`, `freetype`; UI использует Qt/MSBuild integration.

Для локальной проверки собран Release x64 executable `build/Planet/Release/Planet.exe`. Запускать из каталога `Planet`, чтобы находились ресурсы; в `Planet.vcxproj.user` этот рабочий каталог задан для отладчика.

## Режимы запуска

- Обычный GUI: без специальных аргументов.
- Benchmark: `--benchmark` в argv/Windows command line или environment `GAME_NEW_BENCHMARK`. Используется `QCoreApplication`, GUI не создаётся.
- Передача DAG: `--dag-transport-benchmark output.csv` — CPU-замеры L2/L4/L6 и полные SHA-256 результатов, без GUI/GPU. Для сравнения разных процессов задавать `QT_HASH_SEED=0`. Проверяются актуальность результатов, повтор, возврат входов, вытеснение кэша, перемещение backend и lifetime общих ссылок.
- Contributor: compile-time `kContributorMode=true` в `core/AppViewConfig.h`, затем rebuild.

## Benchmark

`runDagBackendBenchmark` записывает `dag_backend_benchmark_results.csv` и сравнивает:

- DAG terrain vs Legacy terrain для climate L2, changed seed L2, perlin L3;
- DAG scene vs direct legacy scene-derived operations;
- baseline/repeat/selection/visual/terrain change/revert;
- executed/skipped nodes, value cache и plan cache.

Compatibility terrain проверяет level, generator, параметры генерации, cell count и все поля каждой клетки. Scene compatibility значительно слабее: она не сравнивает массивы по значениям, а проверяет наличие terrain и факт выполнения/skip DAG nodes. Benchmark не заменяет визуальную проверку сцены.

## Тесты

- `tests/scene_integration.cpp` — lifecycle маленького ECS, script update, selection, destroy.
- `tests/dag_backend_benchmark.cpp` — создаёт CSV, проверяет report ok и наличие строк DAG/legacy/statistics.
- `tests/DagTransportBenchmark.cpp` — прямое сравнение маршрутов с A*, проверка Scene против свежего backend и контрольные суммы всех полей снимка, всех placements и маршрутов для before/after.
- `tools/model_cache_test.cpp` — отдельный model cache tool.

CPU-проверки не покрывают OpenGL rendering, picking и реальное движение машины. Контрольные суммы Scene фиксируют эквивалентность до/после смены транспорта, но не доказывают правильность самого алгоритма расстановки.

## Изменение source/assets

Проект перечисляет `.cpp/.h` явно. При добавлении файла обновить `.vcxproj` и `.vcxproj.filters`. Assets живут в `resources`; многие пути runtime относительные, поэтому current working directory должен позволять найти `resources/...`.
