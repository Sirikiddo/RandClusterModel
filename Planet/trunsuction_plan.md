# Статус и план встраивания ProcessDAG в Planet

## Актуальный статус

Первые три milestone выполнены:

- `ProcessDAG` встроен в `third_party/ProcessDAG`, подключён через `ProcessDag.Embedded.props` и используется через публичные заголовки `proc`;
- `EngineFacade` является активной границей для terrain regeneration, pathfinding и производных данных сцены;
- `DagTerrainBackend` строит `TerrainSnapshot`, а `ITerrainSceneBridge` проецирует его в `HexSphereSceneController`;
- `DagSceneBackend` рассчитывает и кэширует selection outline, tree placements и model placements;
- `DagPathBackend` получает актуальный terrain snapshot и параметры сглаживания пути.

За пределами DAG намеренно остаются Qt events, камера, OpenGL upload/state, frame time, water optics и аналитическая анимация волн. Water coast/hydrology являются render-facing производными текущего `HexSphereModel` и пересобираются после применения terrain snapshot.

Оставшаяся миграция касается прежде всего прямого редактирования высот/биомов и high-level команд сущностей в `InputController`. Разделы ниже сохранены как план оставшихся этапов, а не как описание текущего состояния.

## Контекст

- Цель: встроить `C:\Users\User\source\repos\ProcessDAG\ProcessDag` в `Planet` как backend/runtime слой, чтобы убрать часть ручной orchestration-логики и получить более формализованный pipeline состояния.
- Текущий статус `Planet`: backend-граница работает через `EngineFacade`, но часть ручных world mutations всё ещё живёт в `InputController` и `HexSphereSceneController`.
- Текущий статус `ProcessDAG`: terrain, path и часть derived scene pipeline уже используют embedded runtime.

## Оценка сложности

- Общая сложность: выше средней, ближе к высокой.
- Оценка по масштабу:
  - подключить `ProcessDAG` как зависимость в solution: низкая;
  - собрать `Planet` с поддержкой публичного модуля `Proc`: низкая-средняя;
  - перевести текущую domain-логику `Planet` на декларативный DAG runtime: высокая;
  - стабилизировать UX, рендер и интерактивные сценарии после миграции: средняя-высокая.
- Причина: сложность в основном архитектурная, а не build-system. Сейчас `Planet` хранит и мутирует state прямо в UI/controller слое, поэтому нужен явный слой адаптации между imperative-логикой и DAG execution model.

## Главные риски

- Размытая граница backend/frontend в `Planet`.
- Сильная связность `InputController` с `HexSphereSceneController`, ECS и renderer upload path.
- Потенциальное дублирование состояния: часть мира в `ProcessDAG`, часть в старых контроллерах.
- Embedded ProcessDAG увеличивает связность project metadata: изменения состава runtime требуют синхронной проверки Debug/Release и обоих `.vcxproj`.

## Что уже видно по коду

- В `Planet` около 80 исходных файлов вне build-артефактов.
- В `ProcessDAG` около 67 исходных файлов вне build-артефактов.
- `Planet.vcxproj` использует embedded integration:
  - исходники и публичные заголовки находятся в `third_party/ProcessDAG`;
  - импортируется `ProcessDag.Embedded.props`;
  - `EnableModules`, `BuildStlModules`, `ScanSourceForModuleDependencies` и `TranslateIncludes=false` согласованы для Debug и Release;
  - backend-код подключает стабильные публичные заголовки `<proc/...>`.

## Рекомендуемая стратегия миграции

- Не делать big bang replacement.
- Сначала встроить `ProcessDAG` как отдельный runtime backend за адаптером.
- Держать `HexSphereSceneController` и renderer как временный consumer состояния.
- Переводить use-case'ы по одному: генерация мира, выбор клетки, изменение высоты, смена биома, pathfinding/команды сущностей.

## Поэтапный план

### Этап 1. Подготовка сборки — выполнен

- ProcessDAG встроен локально через `third_party/ProcessDAG/ProcessDag.Embedded.props`.
- Module settings согласованы в Debug и Release.
- Backend использует публичные заголовки `<proc/...>`; локальная копия не требует внешнего project reference.

### Этап 2. Выделение backend seam в Planet — частично выполнен

- `EngineFacade` уже является точкой доступа к terrain regeneration, pathfinding и derived scene DAG.
- Перенести из `InputController` команды уровня backend:
  - rebuild terrain — выполнено;
  - mutate cell state;
  - set generator params — выполнено;
  - high-level entity move command;
  - selection/path requests — path и selection outline выполнены, selection state пока хранится сценой.
- Зафиксировать контракт:
  - вход: команды/интенты;
  - выход: snapshot/read-model для renderer и UI overlay.

### Этап 3. Описание данных Planet в терминах ProcessDAG — частично выполнен

- Сопоставить текущие структуры `Planet` со schema/runtime-моделью `ProcessDAG`.
- Определить минимальный набор узлов:
  - world config;
  - terrain generation params;
  - planet mesh/model snapshot;
  - selection state;
  - entity commands/state;
  - derived render data или промежуточные view-model.
- Решить заранее, что остается вне DAG:
  - OpenGL upload;
  - Qt events;
  - camera math;
  - низкоуровневый renderer state.

### Этап 4. Первый вертикальный срез — выполнен

- Реализован end-to-end сценарий через `ProcessDAG`:
  - изменение `TerrainParams`;
  - rebuild terrain;
  - получение snapshot;
  - адаптация snapshot обратно в `HexSphereSceneController` или новый render-facing state.
- Сценарий является основным путём terrain regeneration в текущем приложении.

### Этап 5. Миграция мутаций мира — следующий этап

- Перевести операции редактирования клеток:
  - высота;
  - биом;
  - сброс/перегенерация.
- Убрать прямые мутации модели из `InputController`.
- Оставить в `InputController` только orchestration ввода и вызовы facade/runtime.

### Этап 6. Миграция сущностей и команд — не начат

- Перевести high-level команды сущностей в backend runtime.
- Отдельно решить, анимация движения живет:
  - полностью вне DAG;
  - или DAG считает маршрут/целевое состояние, а визуальная анимация остается в ECS/render layer.
- Предпочтительный вариант для первого прохода:
  - DAG отвечает за решение и целевое состояние;
  - текущая ECS-анимация остается визуальным слоем.

### Этап 7. Консолидация и зачистка legacy path — выполняется постепенно

- После нескольких migrated use-case'ов удалить дублирующее состояние.
- Свести `HexSphereSceneController` к read-model/service для renderer либо разрезать его на:
  - runtime adapter;
  - geometry builder;
  - render snapshot builder.
- Упростить `InputController`, чтобы он перестал быть местом хранения domain-логики.

## Выполненные первые milestone

- [x] Milestone A: `Planet` собирается с embedded ProcessDAG и использует публичный API `proc`.
- [x] Milestone B: `EngineFacade` выполняет `rebuild terrain` через `DagTerrainBackend`.
- [x] Milestone C: изменение terrain parameters → DAG regeneration → snapshot projection проходит через новый backend path.

## Ориентир по трудоемкости

- Build integration: 0.5-1 день.
- Выделение facade и backend seam: 1-3 дня.
- Первый рабочий vertical slice через `ProcessDAG`: 2-5 дней.
- Перевод основных world mutations и стабилизация: 5-10+ дней.

## Критерии успеха

- UI/renderer больше не мутируют domain state напрямую.
- `EngineFacade` становится реальной boundary между Qt/OpenGL и backend runtime.
- `ProcessDAG` хранит и пересчитывает значимую часть состояния мира.
- Snapshot/read-model для рендера строится детерминированно и без дублирования источников истины.

## Следующий практический шаг

- Перенести ручные мутации высоты/биома и high-level entity intents за `EngineFacade`, сохраняя визуальную анимацию, water frame state и OpenGL вне DAG.
