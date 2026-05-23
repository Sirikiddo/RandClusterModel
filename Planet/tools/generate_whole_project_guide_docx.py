from __future__ import annotations

from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "whole_project_guide_ru_utf8.docx"


def para(text: str, heading: int = 0) -> str:
    t = escape(text)
    if heading == 1:
        return (
            "<w:p><w:pPr><w:spacing w:before='160' w:after='80'/></w:pPr>"
            "<w:r><w:rPr><w:b/><w:sz w:val='32'/></w:rPr>"
            f"<w:t xml:space='preserve'>{t}</w:t></w:r></w:p>"
        )
    if heading == 2:
        return (
            "<w:p><w:pPr><w:spacing w:before='120' w:after='40'/></w:pPr>"
            "<w:r><w:rPr><w:b/><w:sz w:val='26'/></w:rPr>"
            f"<w:t xml:space='preserve'>{t}</w:t></w:r></w:p>"
        )
    return (
        "<w:p><w:pPr><w:spacing w:after='60'/></w:pPr>"
        "<w:r><w:rPr><w:sz w:val='22'/></w:rPr>"
        f"<w:t xml:space='preserve'>{t}</w:t></w:r></w:p>"
    )


sections = [
    para("Большая методичка по всему проекту Planet5", 1),
    para("Этот документ объясняет архитектуру проекта целиком: из каких подсистем он состоит, как приложение запускается, как строится планета, как обрабатывается пользовательский ввод, как устроен рендер, как работают объекты на сфере, как используется ECS, зачем введена DAG-подсистема и что лежит в основных папках проекта."),

    para("1. Что это за проект в целом", 2),
    para("Planet5 — это настольное Qt/OpenGL-приложение на C++, в котором отображается сферическая планета, разбитая на ячейки, похожие на шестиугольники и пятиугольники. На этой планете можно генерировать рельеф, биомы, воду, деревья, выбирать клетки, строить путь, размещать модели и взаимодействовать с сущностями."),
    para("Проект построен как набор нескольких слоёв: слой UI, слой контроллеров, слой модели данных планеты, слой генерации, слой рендера и слой вспомогательной архитектуры для ECS и terrain backend. Из-за этого проект кажется большим, но если смотреть на него по потокам данных, он устроен довольно логично."),

    para("2. Главный поток запуска приложения", 2),
    para("Точка входа — файл core/main.cpp. В нём настраивается OpenGL-контекст через QSurfaceFormat: версия 3.3, core profile, depth buffer, stencil buffer и multisampling. После этого создаётся QApplication и главное окно MainWindow."),
    para("Затем вызывается defaultAppViewConfig из core/AppViewConfig.h. Этот конфиг определяет, в каком режиме запускается приложение: обычный режим планеты или contributor mode. Сейчас по умолчанию активен обычный режим Planet."),
    para("После этого создаётся MainWindow, задаётся размер окна и вызывается show. На этом этапе управление переходит в Qt event loop через app.exec()."),

    para("3. Что делает MainWindow", 2),
    para("MainWindow — это главный компоновщик интерфейса. Он не рисует планету сам и не генерирует рельеф сам. Его задача — собрать вместе крупные блоки приложения."),
    para("В конструкторе MainWindow создаются CameraController и InputController. Это означает, что отдельный объект отвечает за камеру, а отдельный — за обработку ввода и игровую логику."),
    para("Потом создаётся центральный OpenGL-виджет HexSphereWidget. Именно он становится главным содержимым окна через setCentralWidget."),
    para("Также MainWindow строит верхнюю toolbar-панель с выбором subdivision level, кнопкой сброса камеры и кнопкой очистки выделения. Кроме этого создаётся правый dock Planet Settings, в котором живёт PlanetSettingsPanel."),
    para("MainWindow связывает UI через Qt signals и slots: смена subdivision передаётся в HexSphereWidget, нажатие Reset View идёт в resetView, настройки генерации и визуализации из PlanetSettingsPanel тоже передаются в HexSphereWidget."),

    para("4. Роль HexSphereWidget", 2),
    para("HexSphereWidget — это центральный визуальный узел проекта. Он наследуется от QOpenGLWidget, поэтому умеет и принимать пользовательские события, и жить как обычный Qt-виджет, и рисовать 3D-сцену через OpenGL."),
    para("Внутри HexSphereWidget хранятся ссылки на CameraController и InputController. Также в обычном режиме он создаёт EngineFacade — это фасад поверх terrain backend, который помогает синхронизировать состояние сцены и подготовку terrain mesh."),
    para("Именно здесь происходит инициализация OpenGL, запуск таймеров, отрисовка кадра, показ HUD-подсказок и создание пользовательской панели карточек Build/Factory/Mine/Delete."),
    para("HexSphereWidget можно понимать как мост между Qt-окном и всей внутренней логикой сцены."),

    para("5. Как устроен UI в проекте", 2),
    para("UI делится на две части. Первая часть — стандартный Qt-интерфейс: MainWindow, toolbar, status bar, dock с настройками. Вторая часть — overlay UI поверх 3D-сцены внутри HexSphereWidget."),
    para("PlanetSettingsPanel — это обычная QWidget-панель с QComboBox, QSpinBox, QDoubleSpinBox, QCheckBox и QPushButton. Она не изменяет модель сцены напрямую. Вместо этого она только эмитит сигналы generatorChanged, paramsChanged, visualizeChanged и requestRegenerate."),
    para("Панель размещения объектов в HexSphereWidget тоже не работает со сценой напрямую. Она только меняет активный инструмент в InputController."),
    para("Такое разделение делает код чище: UI сообщает о намерении пользователя, а игровая логика уже решает, что с этим делать."),

    para("6. Как работает камера", 2),
    para("Класс CameraController отвечает за матрицы view и projection, а также за поворот и зум камеры."),
    para("Метод resize перестраивает перспективную матрицу projection в зависимости от размеров окна и device pixel ratio. Это нужно, чтобы картинка не искажалась при изменении размера окна."),
    para("Метод rotate меняет quaternion sphereRotation_. Важно, что здесь вращается не камера вокруг мира в классическом FPS-смысле, а визуально вращается сцена относительно камеры."),
    para("Метод zoom меняет distance_ и ограничивает её, чтобы камера не улетала слишком далеко и не подходила слишком близко."),
    para("Методы rayOrigin и rayDirectionFromScreen используются для выбора объектов мышью. Они позволяют превратить 2D-координату курсора на экране в 3D-луч в пространстве сцены."),

    para("7. Роль InputController", 2),
    para("InputController — это один из самых важных классов проекта. Он связывает пользовательский ввод, данные планеты, ECS и рендерер."),
    para("Он принимает события мыши и клавиатуры из HexSphereWidget, определяет, куда именно пользователь нажал, управляет выделением клеток, выбором сущностей, перемещением машинки, размещением зданий, удалением зданий, а также координирует загрузку буферов в рендерер."),
    para("Внутри InputController живут три важные вещи: CameraController, HexSphereSceneController и ComponentStorage. То есть он одновременно знает о камере, о самой планете и об объектной системе."),
    para("Именно этот класс реализует интерфейс ITerrainSceneBridge, через который terrain backend может обращаться к состоянию сцены."),

    para("8. Как устроена сцена планеты", 2),
    para("HexSphereSceneController — это класс, который хранит и обновляет модель самой планеты. Он владеет IcosphereBuilder, IcoMesh, HexSphereModel, TerrainMesh, генератором рельефа и настройками визуализации."),
    para("Если говорить просто, то HexSphereSceneController — это управляющий объект для данных планеты: он знает текущий subdivision level, выбранный terrain generator, параметры генерации, набор выбранных клеток и размещения деревьев."),
    para("При rebuildModel или regenerateTerrain он перестраивает топологию, запускает генератор рельефа, пересчитывает mesh и обновляет размещение деревьев."),
    para("Также он умеет строить wireframe-вершины, контур выделения, водную геометрию и path polyline."),

    para("9. Что такое HexSphereModel", 2),
    para("HexSphereModel — это центральная модель данных планеты на уровне клеток. Здесь хранится геометрическая и логическая структура сферической сетки."),
    para("Сначала строится икосфера. Затем из неё формируется dual-сетка, в которой вершинам исходной икосферы соответствуют клетки будущей планеты. Каждая клетка содержит соседей, polygon boundary, centroid, биом, высоту, климатические данные и параметры руды."),
    para("Именно из HexSphereModel потом строятся terrain mesh, wireframe, water geometry и pick triangles."),
    para("Если объяснять преподавателю простыми словами, то HexSphereModel — это «умная карта планеты», где каждая клетка знает всё о себе и о соседях."),

    para("10. Генерация рельефа и биомов", 2),
    para("Генерация вынесена в подсистему generation. В TerrainGenerator.h задаётся интерфейс ITerrainGenerator и конкретные реализации: NoOp, Sine, Perlin и ClimateBiome."),
    para("Это значит, что тип генератора выбирается стратегией. HexSphereSceneController не знает конкретные детали каждого алгоритма. Он просто хранит указатель на ITerrainGenerator и вызывает generate."),
    para("TerrainParams содержат входные параметры генерации: seed, seaLevel и scale."),
    para("ClimateBiomeGenerator и PerlinNoise добавляют климатическую и шумовую составляющую. Благодаря этому можно строить не просто высоту, а ещё и биомы, влажность, температуру и давление."),
    para("На практике поток такой: сцена выбирает генератор по индексу, передаёт ему HexSphereModel и TerrainParams, а генератор уже заполняет данные клеток."),

    para("11. Как из модели получается mesh", 2),
    para("После того как HexSphereModel заполнен, из него строится terrain mesh. Этой частью занимаются генераторы мешей в папке generation/MeshGenerators и вспомогательные рендер-пайплайны."),
    para("TerrainMeshPolicy задаёт правила построения поверхности. WireMeshGenerator строит линии каркаса. SelectionOutlineGenerator строит геометрию контура выделенных клеток. WaterMeshGenerator строит отдельную геометрию воды."),
    para("В результате проект хранит не только логические клетки, но и подготовленные массивы вершин, нормалей, цветов и индексов, которые уже можно отправить в OpenGL."),

    para("12. Как работает ECS в проекте", 2),
    para("Подсистема ECS лежит в папке ECS. Здесь проект использует лёгкую собственную реализацию Entity Component Storage, а не внешний движок."),
    para("ComponentStorage хранит сущности и отдельные таблицы компонентов: Transform, Mesh, Collider, Material, Script и Animation."),
    para("Когда создаётся сущность, ей выдаётся id. После этого через emplace можно добавить нужные компоненты. Например, машинке добавляется Mesh с meshId car, Transform с позицией и Collider для выбора мышью."),
    para("Метод each позволяет проходить по всем сущностям, у которых есть нужный набор компонентов. Это очень удобно для рендера и для логики выбора."),
    para("Метод update запускает обновление скриптов и анимаций. Благодаря этому ECS используется в проекте не только как хранилище, но и как простая система поведения."),

    para("13. Какие сущности реально есть в сцене", 2),
    para("В обычном режиме при инициализации InputController создаёт Explorer — это машинка, которая может перемещаться по поверхности планеты."),
    para("Через пользовательскую панель создаются сущности Factory и Mine. Для них в ECS добавляются Mesh, Transform и Collider."),
    para("При активации режима Delete соответствующая сущность может быть удалена через ecs_.destroyEntity."),
    para("Таким образом ECS в этом проекте в первую очередь используется для динамических объектов, которые существуют поверх terrain mesh."),

    para("14. Как работает выбор мышью", 2),
    para("Для выбора используется лучевой pick. CameraController выдаёт начало луча и его направление. Дальше InputController проверяет пересечение луча с поверхностью и с сущностями."),
    para("Для поверхности используется pickTerrainAt. Луч перебирается по треугольникам terrain mesh, и для проверки пересечения используется алгоритм rayTriangleMT."),
    para("Для сущностей используется pickEntityAt. Там каждая сущность с Collider рассматривается как сфера коллизии."),
    para("Метод pickSceneAt объединяет оба результата и выбирает ближайший. За счёт этого один и тот же клик мышью можно интерпретировать либо как выбор клетки, либо как выбор модели на поверхности."),

    para("15. Как работает размещение и удаление объектов", 2),
    para("В InputController введено перечисление PlacementModel. Оно хранит режим None, Factory, Mine или Delete."),
    para("Когда пользователь выбирает карточку на панели, HexSphereWidget передаёт новый режим в InputController через setPlacementModel."),
    para("Если активен Factory или Mine, при клике по поверхности вызывается placeBuildingOnCell. Этот метод проверяет, что клетка существует и что она свободна. Затем создаёт новую сущность в ECS и вычисляет её позицию через функцию вычисления точки на поверхности сферы."),
    para("Если активен Delete, вместо создания вызывается deleteEntityAtHit. Этот метод разрешает удалять только фабрику и рудник, но не машинку."),
    para("Дополнительно в проект встроен запрет на перемещение статичных зданий. Проверка делается через isMovableEntity: если meshId равен factory или mine, перемещать такой объект нельзя."),

    para("16. Как работает путь и движение машинки", 2),
    para("В controllers/PathBuilder реализована логика построения пути между клетками. Он использует соседей клеток и параметры smooth mode."),
    para("Когда пользователь выбирает клетки или запускает движение, InputController может строить path polyline и загружать её в рендерер."),
    para("Метод applyAnimation создаёт компонент Animation для сущности и рассчитывает путь по поверхности. Затем ECS обновляет эту анимацию по времени, а машинка перемещается по точкам полилинии."),
    para("При этом направление движения хранится в surfaceForward, чтобы модель машинки корректно ориентировалась на поверхности сферы."),

    para("17. Как устроен рендер", 2),
    para("Главный класс рендера — HexSphereRenderer. Он создаёт и хранит OpenGL-программы, буферы, VAO, модели и вспомогательные рендереры."),
    para("Он умеет загружать в GPU wireframe, terrain mesh, selection outline, path polyline и water geometry. Также он хранит инициализированные модели деревьев, машины, фабрики и рудника."),
    para("Во время renderScene ему передаётся RenderGraph, который содержит сцену, ECS и параметры высоты, а также RenderCamera и SceneLighting."),
    para("Фактически HexSphereRenderer — это оркестратор, а более узкие задачи делегируются классам TerrainRenderer, WaterRenderer, EntityRenderer и OverlayRenderer."),

    para("18. Что делает EntityRenderer", 2),
    para("EntityRenderer отвечает за отрисовку сущностей из ECS. Он проходит по сущностям с компонентами Mesh и Transform и в зависимости от meshId решает, какой draw-метод вызвать."),
    para("Если meshId равен factory, вызывается renderFactory. Если равен mine, вызывается renderMine. Для машинки вызывается renderCar."),
    para("При этом каждая модель сначала ориентируется по нормали поверхности сферы, а потом получает локальные поправки масштаба и положения."),
    para("То есть рендер объектов на планете строится не как плоская сцена, а как набор моделей, привязанных к сферической поверхности."),

    para("19. Роль ресурсов", 2),
    para("В папке resources лежат 3D-модели и вспомогательные файлы. Например, resources/car содержит obj, mtl и текстуры для машины. Resources/factory и resources/mine содержат соответствующие модели и текстуры строений."),
    para("В resources/models лежат jpg-превью для UI-карточек."),
    para("В resources/HexSphereWidget_shaders.h лежат строковые исходники шейдеров, которые используются рендерером."),
    para("Также в resources лежат модели деревьев и qrc-файлы Qt. То есть эта папка совмещает и runtime-ресурсы 3D-сцены, и часть ресурсов интерфейса."),

    para("20. Что делает DAG-подсистема", 2),
    para("Папка dag — это отдельный слой для абстракции terrain backend. Здесь проект отделяет работу с terrain snapshot и mesh-подготовкой от UI и рендера."),
    para("ITerrainSceneBridge описывает интерфейс, через который backend может попросить сцену подготовить snapshot, применить snapshot и перестроить terrain по staged-параметрам."),
    para("EngineFacade — это фасад, который снаружи выглядит как единый backend-объект. Он умеет attachTerrainBridge, initializeTerrainState, setTerrainParams, regenerateTerrain и prepareTerrainMesh."),
    para("Внутри могут использоваться разные backend-реализации. DagTerrainBackend — это вариант с DAG-подходом. LegacyTerrainBackend — более прямой вариант без DAG-пути."),
    para("Практически это означает, что проект уже готов к более сложной архитектуре пересборки terrain, и UI работает не напрямую с генераторами, а через фасадный слой."),

    para("21. Что такое contributor mode", 2),
    para("В AppViewConfig есть переключатель contributor mode. Если он включён, приложение работает не как полноценная планета, а как специальный режим просмотра contributor-сцены."),
    para("В этом режиме часть возможностей отключается: например, настройки генерации и часть игровых взаимодействий. Это видно и в MainWindow, и в контроллерах."),
    para("Contributor mode нужен как отдельный режим демонстрации или изолированного просмотра, не завязанного на всю логику планеты."),

    para("22. Зачем папка scene, если уже есть ECS", 2),
    para("В папке scene лежит альтернативный слой scene graph, где сущности хранятся как shared_ptr и есть события spawn, destroy и update."),
    para("Судя по коду и комментариям, эта часть сохранена как legacy helper или как более ранняя архитектура сцены. Сейчас для основной логики интерактивных объектов проект больше использует ECS."),
    para("То есть scene graph здесь играет вспомогательную роль и, вероятно, был частью предыдущих шагов развития проекта."),

    para("23. Что лежит в tests и docs", 2),
    para("В tests есть scene_integration.cpp. Это небольшой интеграционный тест для ECS. Он проверяет жизненный цикл сущностей, работу selectedEntity, destroyEntity и update script callbacks."),
    para("В docs уже лежат небольшие вспомогательные markdown-файлы, например scene_example.md. Они помогают понять, как использовать ECS и какие идеи закладывались в архитектуру."),
    para("То есть документация и тесты в проекте пока не полные, но уже есть задел для объяснения ключевых подсистем."),

    para("24. Что лежит в third_party, include, lib, glm, x64 и GameNew", 2),
    para("Папка third_party содержит внешние зависимости, например ProcessDAG. Это сторонний код, на который опирается DAG-часть проекта."),
    para("Папки include, lib и glm содержат подключаемые библиотеки и заголовки, например OpenGL, GLFW, GLEW и GLM."),
    para("Папка x64 содержит результаты сборки и промежуточные артефакты Visual Studio."),
    para("Папка GameNew и связанные с ней qt/moc/uic/rcc-файлы выглядят как сгенерированные Qt-артефакты или наследие прошлой конфигурации проекта."),
    para("Преподавателю можно объяснить это так: не все папки содержат ручную бизнес-логику. Часть из них — это зависимости, автогенерация или build artifacts."),

    para("25. Как проходит один кадр приложения", 2),
    para("Пользовательский интерфейс работает в цикле Qt. Когда нужно перерисовать окно, HexSphereWidget вызывает paintGL."),
    para("Внутри paintGL вычисляется dt, при необходимости вызывается engine_->tick, затем через InputController::render рисуется сцена."),
    para("InputController формирует RenderGraph из сцены, ECS и параметров высоты. Затем он передаёт его в HexSphereRenderer вместе с view/projection от камеры и освещением."),
    para("Рендерер рисует terrain, воду, сущности, оверлеи и статистику. После этого поверх OpenGL-кадра обычный QPainter может дорисовать 2D-текст overlay."),

    para("26. Как объяснить весь проект преподавателю коротко", 2),
    para("Можно объяснять так: проект состоит из окна Qt, внутри которого есть OpenGL-виджет. Этот виджет получает ввод пользователя и передаёт его в InputController. InputController управляет сценой планеты, объектами и выбором клеток."),
    para("Сама планета хранится как HexSphereModel и управляется через HexSphereSceneController. Генераторы terrain заполняют данные клеток, после чего из них строятся mesh-структуры для рендера."),
    para("Объекты на планете — машинка, фабрика и рудник — живут в ECS. Их выбирают через ray picking, размещают через placement modes и рисуют через EntityRenderer."),
    para("OpenGL-рендер сосредоточен в HexSphereRenderer и связанных с ним рендер-классах. Отдельно от этого существует backend-абстракция через EngineFacade и DAG-подсистему."),

    para("27. Итоговая карта проекта по папкам", 2),
    para("core — старт приложения и базовый конфиг режима."),
    para("ui — Qt-интерфейс, OpenGL-виджет, панели настроек и статистика."),
    para("controllers — камера, ввод, сцена, путь и игровая координация."),
    para("model — геометрическая и логическая модель сферической планеты и связанные data structures."),
    para("generation — генераторы terrain, климата, шума и mesh builders."),
    para("renderers — OpenGL-отрисовка terrain, воды, сущностей и видимости."),
    para("ECS — система сущностей и компонентов для динамических объектов."),
    para("dag — terrain backend abstraction и фасад для DAG/legacy-пути."),
    para("scene — legacy scene graph и связанные сущности."),
    para("resources — модели, текстуры, превью и шейдеры."),
    para("tests — тесты подсистем."),
    para("third_party, include, lib, glm — внешние зависимости и подключаемые библиотеки."),

    para("28. Финальный вывод", 2),
    para("Проект Planet5 — это не просто один OpenGL-файл, а многослойное приложение. В нём есть отдельно UI на Qt, отдельно контроллеры, отдельно модель планеты, отдельно генерация terrain, отдельно ECS для объектов и отдельно рендер. За счёт этого проект можно объяснять по цепочке: запуск приложения, создание окна, ввод пользователя, обновление сцены, генерация геометрии, рендер кадра и работа с объектами на поверхности планеты."),
]


document_xml = f"""<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:wpc="http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas"
 xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006"
 xmlns:o="urn:schemas-microsoft-com:office:office"
 xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"
 xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"
 xmlns:v="urn:schemas-microsoft-com:vml"
 xmlns:wp14="http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing"
 xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
 xmlns:w10="urn:schemas-microsoft-com:office:word"
 xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
 xmlns:w14="http://schemas.microsoft.com/office/word/2010/wordml"
 xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup"
 xmlns:wpi="http://schemas.microsoft.com/office/word/2010/wordprocessingInk"
 xmlns:wne="http://schemas.microsoft.com/office/word/2006/wordml"
 xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape"
 mc:Ignorable="w14 wp14">
  <w:body>
    {' '.join(sections)}
    <w:sectPr>
      <w:pgSz w:w="11906" w:h="16838"/>
      <w:pgMar w:top="1134" w:right="1134" w:bottom="1134" w:left="1134" w:header="708" w:footer="708" w:gutter="0"/>
    </w:sectPr>
  </w:body>
</w:document>
"""

content_types = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
"""

rels = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
"""

OUT.parent.mkdir(parents=True, exist_ok=True)
if OUT.exists():
    OUT.unlink()

with ZipFile(OUT, "w", ZIP_DEFLATED) as zf:
    zf.writestr("[Content_Types].xml", content_types.encode("utf-8"))
    zf.writestr("_rels/.rels", rels.encode("utf-8"))
    zf.writestr("word/document.xml", document_xml.encode("utf-8"))

print(OUT)
