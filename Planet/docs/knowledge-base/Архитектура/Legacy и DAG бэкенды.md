---
tags: [gamenew, dag, legacy, backend]
---

# Legacy и DAG бэкенды

## Выбор backend

В [TerrainBackendSelector.h](../../../dag/TerrainBackendSelector.h):

```cpp
using SelectedTerrainBackend = DagTerrainBackend;
```

Это compile-time выбор. Чтобы вернуть legacy terrain, alias меняется на `LegacyTerrainBackend`; оба проверяются concept `TerrainBackend`.

## Общий контракт terrain

`ITerrainSceneBridge` отделяет backend от живой сцены:

- stage params / generator / subdivision;
- rebuild terrain from staged inputs;
- capture snapshot;
- project snapshot.

`InputController` реализует bridge, делегируя в `HexSphereSceneController`.

## LegacyTerrainBackend

Legacy не строит модель сам. Setter сразу stage-ит значение в bridge и синхронизирует собственный snapshot из сцены. `regenerateTerrain()` вызывает `bridge->rebuildTerrainFromInputs()`:

```mermaid
sequenceDiagram
  participant UI
  participant L as LegacyTerrainBackend
  participant B as InputController bridge
  participant S as SceneController
  UI->>L: set params/generator/level
  L->>B: stage...
  UI->>L: regenerateTerrain()
  L->>B: rebuildTerrainFromInputs()
  B->>S: legacy generator mutates model
  L->>B: captureTerrainSnapshot()
```

Плюсы: минимум serialization и дублирования topology. Минус: backend тесно зависит от живой сцены и всегда пересчитывает производные данные монолитно.

## DagTerrainBackend

Схема содержит inputs `generatorIndex`, `seed`, `seaLevel`, `scale`, `subdivisionLevel` и два последовательных node: `TerrainBuild/buildTerrain` создаёт `baseTerrainSnapshot`, затем `OreBuild/generateOre` добавляет отдельное поле залежей и выдаёт итоговый `terrainSnapshot`.

Executor:

1. строит `IcosphereBuilder::build(level)`;
2. создаёт новый `HexSphereModel`;
3. запускает выбранный terrain generator;
4. превращает клетки в base snapshot;
5. второй node восстанавливает topology и запускает `OreGenerator`;
6. публикует финальный snapshot как `shared_ptr<const TerrainSnapshot>` в адаптере текущего запуска; commit содержит только короткий токен.

Backend создаёт `DefaultDagEngine` и отдельный адаптер на regeneration, `init` inputs → `flush_prepare` → разрешает токен `terrainSnapshot` в общую ссылку → `ack_outputs` → project через bridge. Setter-ы только меняют staged state backend, не живую сцену.

Это временная передача внутри одного процесса с одним следующим потребителем каждого снимка. ProcessDAG по-прежнему хранит строки; адаптер живёт в пределах запуска, а готовый снимок удерживается backend через общую ссылку. Для нескольких процессов или независимых потребителей нужен явный контракт typed payloads. JSON-сериализация снимков сохранена как отдельный формат и в проверках его совместимости, но больше не используется для передачи данных в Terrain/Scene/Path DAG. Повторное построение topology и копия base snapshot для расчёта руды этим изменением не устранены.

> [!important] Runtime
> В обычном Planet mode именно этот путь активен. После projection `HexSphereSceneController` заново строит topology, копирует cell fields, перестраивает terrain mesh и деревья.

## DagPathBackend

Это отдельный долгоживущий DAG engine с inputs snapshot, `smoothMaxDelta`, start и goal; node `FindPath` реконструирует модель и вызывает обычный `PathBuilder`. DAG здесь orchestration/cache boundary, сам алгоритм A* остаётся legacy C++ классом.

Снимок и `PathResult` хранятся в `InProcessDagPayloads<T>` через `shared_ptr<const T>`. В DAG остаются прежние ключи `terrainSnapshot` и `pathResult`, их значения — короткие токены. Повторная передача неизменного снимка сравнивает поля структуры и переиспользует токен без JSON. Это не кэш готового навигационного графа: выполняющийся узел по-прежнему строит модель и граф заново.

## DagSceneBackend

Три guarded node:

| Node | Зависимости | Output |
|---|---|---|
| `BuildSelectionOutline` | terrain + selected + visual | float array линий |
| `BuildTreePlacements` | terrain | placements деревьев |
| `BuildModelPlacements` | terrain + visual + ECS requests | позиция/up сущностей |

Снимки, selection, visual params, model requests и результаты передаются как типизированные неизменяемые структуры через адаптер. Dirty определяется по компактным токенам входов. Для поиска уже известного входа адаптер сравнивает все поля структуры; хеши и адреса памяти не используются как доказательство равенства.

Каждое хранилище входов/выходов и каждый кэш результатов удерживают максимум восемь записей с вытеснением давно не использованных значений (выходные токены — в порядке публикации). Возврат к недавнему входу переиспользует результат; вытесненный вход получает новый токен и пересчитывается. Это лимит числа записей, а не размера в байтах. ProcessDAG дополнительно ведёт plan cache. Статистика попадает в нижний overlay. Адаптер рассчитан на синхронные вызовы одного backend; при расширении на независимых потребителей, процессы или асинхронное исполнение нужен другой контракт владения.

> [!warning] Неполная интеграция
> `InputController::refreshSceneDagOutputs()` применяет selection outline и tree placements, но игнорирует `result.modelPlacements`. Позиции ECS всё ещё рассчитываются напрямую через `computeSurfacePoint`.

## Что означает «legacy backend» в проекте

Термин используется в двух смыслах:

1. конкретный `LegacyTerrainBackend` как альтернативная реализация terrain contract;
2. прямые CPU-функции `HexSphereSceneController`, `PathBuilder`, `SelectionOutlineGenerator`, которые DAG executor переиспользует или дублирует.

Отдельного `LegacyPathBackend` нет. Benchmark сравнивает DAG terrain с `LegacyTerrainBackend`, а DAG scene — с прямыми вызовами scene controller.
