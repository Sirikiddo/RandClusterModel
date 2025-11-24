# Желаемая архитектурная схема Planet Engine

## 1. UI (источник ввода)

* Получает действия пользователя.
* Генерирует команды: изменение параметров, выбор объектов, нажатия кнопок.
* Не изменяет сцену напрямую и не запускает вычислений.

```
[ПОЛЬЗОВАТЕЛЬ] → [UI] → команды в PlanetCore
```

## 2. Синхронная граница №1 — фиксация состояния

**PlanetCore::applyInput()**

* Применяет команды UI к внутренним настройкам.
* Увеличивает `sceneVersion`.
* Создаёт неизменяемый снимок сцены/модели.
* Готовит параметры для асинхронных вычислительных задач.
* Запускает асинхронные задачи.

## 3. Async/Parallel слой логики

**AsyncComputeLayer**

* Работает только на копиях (снимках) данных.
* Выполняет тяжёлые вычисления:

  * Генерация мешей
  * Карты высот, шумы, нормали
  * Сглаживание, фильтры
  * Поиск путей
  * Опциональные GPU compute-задачи
* Внутри задач может использовать parallel_for.
* Не имеет доступа к renderer, ECS или изменяемой сцене.
* Возвращает `(result, version)` обратно в PlanetCore.

## 4. Синхронная граница №2 — merge

**PlanetCore::commitAsyncResult()**

* Выполняется в главном потоке.
* Проверяет версию: принимает результат только если версия совпадает с текущей.
* Обновляет данные сцены.
* Формирует RenderGraph.
* Готовит данные для рендера.

## 5. Синхронный слой рендера

**Renderer**

* Использует RenderGraph.
* Обновляет GPU-ресурсы (VBO/IBO/UBO).
* Выполняет команды рисования OpenGL.
* Полностью синхронный.

## 6. Глобальный поток обработки

```
[UI] → applyInput() → async/parallel compute → commitAsyncResult() → render()

          ┌─────────────────────────┐
          │   1. СИНХРОННЫЙ ВВОД    │
          │ UI → applyInput()       │
          └────────────┬────────────┘
                       │ ГРАНИЦА №1
                       ▼
          ┌─────────────────────────┐
          │ 2. ASYNC/PARALLEL ЛОГИКА │
          │ compute(snapshot)       │
          └────────────┬────────────┘
                       │ ГРАНИЦА №2
                       ▼
          ┌─────────────────────────┐
          │    3. СИНХРОННЫЙ РЕНДЕР │
          │ commit → buildGraph → draw │
          └─────────────────────────┘
```

## 7. Архитектурные гарантии

* UI никогда напрямую не изменяет состояние ядра.
* Асинхронные задачи не могут повредить сцену или вызвать гонки.
* Только синхронный merge обновляет состояние мира.
* Renderer всегда получает согласованные, проверенные данные.

## 8. Преимущества

* Чёткое разделение ответственности.
* Детерминированный рендер.
* Безопасное асинхронное и параллельное ускорение.
* Масштабируемый слой логики.
* Предсказуемый процесс для разработчиков.

## 9. Слой управления таймингом (TimingControl)

**TimingControl** — синхронный слой между входом и логикой, контролирующий частоту обновления логики и рендера.

### Назначение

* Определяет, когда можно запускать async-задачи.
* Определяет, когда можно делать merge async-результатов.
* Определяет, нужно ли пропустить логический шаг или кадр рендера.
* Управляет частотой:

  * логики (например 10–30 Гц),
  * рендера (например 30–60 Гц).

### Функции

* `logicStepAllowed()` — можно ли делать логический шаг сейчас.
* `renderAllowed()` — можно ли рендерить кадр.
* `mergeAllowed()` — можно ли принять async-результат.

### Логика поведения

Примеры стратегий:

* Если async-задача занята — пропустить один логический тик.
* Если критичная логика не успевает — заморозить рендер.
* Если очередь async переполнена — отложить новые задачи.

### Встраивание в Pipeline

```
UI → applyInput()
       ↓
 TimingControl.update(dt)
       ↓
if (logicStepAllowed) scheduleAsync()
       ↓
if (asyncReady && mergeAllowed) commitAsync()
       ↓
if (renderAllowed) render()
```

### Роль в архитектуре

Слой тайминга:

* делает производительность управляемой,
* устраняет зависимость логики от частоты рендера,
* отделяет «оба мира» — асинхронный и синхронный,
* делает GlobalPipeline полностью контролируемым и предсказуемым.

## 10. Пример плоского GlobalPipeline в стиле C++ в стиле C++

Ниже — минимальная и максимально понятная реализация конвейера кадра, отражающая всю архитектуру.

```cpp
void GlobalPipeline::tick(float dt)
{
    // ===== 1. СИНХРОННЫЙ ВВОД =====
    applyInput();                // фиксируем команды, обновляем настройки

    // ===== СЛОЙ ТАЙМИНГА =====
    timing.update(dt);           // управляет частотой логики/рендера

    // ===== 2. ЛОГИЧЕСКИЙ ШАГ (ASYNC) =====
    if (timing.logicStepAllowed()) {
        auto snapshot = scene.makeSnapshot();  // неизменяемая копия
        scheduleAsyncCompute(snapshot, sceneVersion);
    }

    // ===== 3. ПРИЁМ ASYNC-РЕЗУЛЬТАТА =====
    if (async.hasResult() && timing.mergeAllowed()) {
        auto [result, version] = async.popResult();
        if (version == sceneVersion) {
            commitAsyncResult(result);          // обновление сцены
        }
    }

    // ===== 4. РЕНДЕР (SYNC) =====
    if (timing.renderAllowed()) {
        RenderGraph graph;
        buildRenderGraph(graph);                // сбор данных в один объект
        renderer.render(graph);                 // строго синхронный OpenGL
    }
}
```

## 12. TaskManager — контейнер асинхронных задач

**TaskManager** — архитектурный контейнер, через который проходят все async/parallel задачи. Он не задаёт стратегий (отмена, приоритеты), но фиксирует место, где эти стратегии будут жить.

### Роль в архитектуре

* Централизованное управление всем асинхронным вычислением.
* Гарантия, что никакой модуль не создаёт потоки напрямую.
* Явная точка входа для политик (ограничение очереди, отмены).
* Предоставляет результаты для sync-merge в `GlobalPipeline`.

### Интерфейс (архитектурный уровень)

```cpp
struct AsyncResult {
    ResultData data;
    uint64_t version;
};

class TaskManager {
public:
    template<typename F>
    void submit(uint64_t version, F&& fn);   // отправить задачу

    bool hasResult() const;                  // есть готовый результат?
    AsyncResult popResult();                 // забрать один результат
};
```

### Встраивание в GlobalPipeline

```cpp
if (timing.logicStepAllowed()) {
    auto snapshot = scene.makeSnapshot();
    uint64_t version = sceneVersion;

    taskManager.submit(version, [snapshot, version]() {
        ResultData r = computeHeavyStuff(snapshot);
        return AsyncResult{ std::move(r), version };
    });
}

if (taskManager.hasResult() && timing.mergeAllowed()) {
    AsyncResult ar = taskManager.popResult();
    if (ar.version == sceneVersion)
        commitAsyncResult(ar.data);
}
```

### Архитектурные гарантии TaskManager

* Все async-вычисления проходят только через него.
* Политики отмены/приоритетов могут меняться без изменения архитектуры.
* `GlobalPipeline` остаётся плоским и детерминированным.

## 11. Пример TimingControl в стиле C++ в стиле C++

```cpp
struct TimingControl {
    float accumLogic  = 0.0f;
    float accumRender = 0.0f;

    float logicRate  = 1.0f / 20.0f;   // 20 Гц логики
    float renderRate = 1.0f / 60.0f;   // 60 Гц рендера

    bool asyncBusy = false;
    bool criticalLogic = false;

    void update(float dt) {
        accumLogic  += dt;
        accumRender += dt;
    }

    bool logicStepAllowed() {
        if (asyncBusy) return false;          // занято async → пропуск шага
        if (accumLogic < logicRate) return false;
        accumLogic -= logicRate;
        return true;
    }

    bool mergeAllowed() {
        return true; // можно добавить условия, например приоритетные задачи
    }

    bool renderAllowed() {
        if (criticalLogic && asyncBusy) return false; // приоритет логики
        if (accumRender < renderRate) return false;
        accumRender -= renderRate;
        return true;
    }
};
```
