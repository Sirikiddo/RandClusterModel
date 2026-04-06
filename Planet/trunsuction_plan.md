# План встраивания ProcessDAG в Planet

## Контекст

- Цель: встроить `C:\Users\User\source\repos\ProcessDAG\ProcessDag` в `Planet` как backend/runtime слой, чтобы убрать часть ручной orchestration-логики и получить более формализованный pipeline состояния.
- Текущий статус `Planet`: backend-граница выражена слабо. `EngineFacade` пока почти пустой, а основная логика состояния живет внутри `InputController` и `HexSphereSceneController`.
- Текущий статус `ProcessDAG`: проект уже подготовлен к embedded-подключению через `ProcessDAG.vcxproj` и публичный модуль `import Proc;`.

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
- C++ modules в `Planet` настроены не полностью симметрично с consumer-требованиями `ProcessDAG`.
- Сборка Qt + modules + project reference может потребовать аккуратной правки `.vcxproj` для обоих конфигураций.

## Что уже видно по коду

- В `Planet` около 80 исходных файлов вне build-артефактов.
- В `ProcessDAG` около 67 исходных файлов вне build-артефактов.
- `ProcessDAG` ожидает consumer path через:
  - project reference на `ProcessDAG.vcxproj`;
  - `EnableModules=true`;
  - `BuildStlModules=true`;
  - `ScanSourceForModuleDependencies=true`;
  - `TranslateIncludes=false`;
  - `import Proc;`.
- В `Planet` это пока совпадает не полностью:
  - `Debug|x64` уже включает `EnableModules=true`;
  - `BuildStlModules` и `TranslateIncludes=false` не выставлены;
  - `Release|x64` не приведен к той же module-конфигурации.

## Рекомендуемая стратегия миграции

- Не делать big bang replacement.
- Сначала встроить `ProcessDAG` как отдельный runtime backend за адаптером.
- Держать `HexSphereSceneController` и renderer как временный consumer состояния.
- Переводить use-case'ы по одному: генерация мира, выбор клетки, изменение высоты, смена биома, pathfinding/команды сущностей.

## Поэтапный план

### Этап 1. Подготовка сборки

- Добавить `ProcessDAG.vcxproj` в solution `Planet`.
- Добавить `ProjectReference` из `Planet.vcxproj` на `ProcessDAG.vcxproj`.
- Привести module settings `Planet` к consumer-требованиям `ProcessDAG` в `Debug|x64` и `Release|x64`.
- Проверить, что минимальный consumer-файл внутри `Planet` собирается с `import Proc;`.

### Этап 2. Выделение backend seam в Planet

- Усилить `EngineFacade`, чтобы он стал единственной точкой доступа UI к backend-операциям.
- Перенести из `InputController` команды уровня backend:
  - rebuild terrain;
  - mutate cell state;
  - set generator params;
  - high-level entity move command;
  - selection/path requests, если их решено вести через runtime.
- Зафиксировать контракт:
  - вход: команды/интенты;
  - выход: snapshot/read-model для renderer и UI overlay.

### Этап 3. Описание данных Planet в терминах ProcessDAG

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

### Этап 4. Первый вертикальный срез

- Сделать один end-to-end сценарий через `ProcessDAG`, не трогая остальной UI:
  - изменение `TerrainParams`;
  - rebuild terrain;
  - получение snapshot;
  - адаптация snapshot обратно в `HexSphereSceneController` или новый render-facing state.
- Это даст реальную оценку стоимости адаптеров и покажет, насколько DAG хорошо ложится на текущую модель мира.

### Этап 5. Миграция мутаций мира

- Перевести операции редактирования клеток:
  - высота;
  - биом;
  - сброс/перегенерация.
- Убрать прямые мутации модели из `InputController`.
- Оставить в `InputController` только orchestration ввода и вызовы facade/runtime.

### Этап 6. Миграция сущностей и команд

- Перевести high-level команды сущностей в backend runtime.
- Отдельно решить, анимация движения живет:
  - полностью вне DAG;
  - или DAG считает маршрут/целевое состояние, а визуальная анимация остается в ECS/render layer.
- Предпочтительный вариант для первого прохода:
  - DAG отвечает за решение и целевое состояние;
  - текущая ECS-анимация остается визуальным слоем.

### Этап 7. Консолидация и зачистка legacy path

- После нескольких migrated use-case'ов удалить дублирующее состояние.
- Свести `HexSphereSceneController` к read-model/service для renderer либо разрезать его на:
  - runtime adapter;
  - geometry builder;
  - render snapshot builder.
- Упростить `InputController`, чтобы он перестал быть местом хранения domain-логики.

## Практический первый milestone

- Milestone A: `Planet` собирается с `ProcessDAG` как project reference и импортирует `Proc`.
- Milestone B: `EngineFacade` получает backend-команду `rebuild terrain` через `ProcessDAG`.
- Milestone C: один пользовательский сценарий полностью проходит через новый backend path.

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

- Начать не с полной миграции, а с подготовки build + первого vertical slice через `EngineFacade`.
