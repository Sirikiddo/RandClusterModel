$ErrorActionPreference = "Stop"

$outputPath = Join-Path $PSScriptRoot "..\docs\guide_panel_placement_ru.docx"
$outputPath = [System.IO.Path]::GetFullPath($outputPath)
$outputDir = Split-Path $outputPath -Parent

if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Escape-Xml {
    param([string]$Text)
    if ($null -eq $Text) { return "" }
    return [System.Security.SecurityElement]::Escape($Text)
}

function New-ParagraphXml {
    param(
        [string]$Text,
        [switch]$Heading1,
        [switch]$Heading2
    )

    $escaped = Escape-Xml $Text

    if ($Heading1) {
        return "<w:p><w:pPr><w:spacing w:before='160' w:after='80'/></w:pPr><w:r><w:rPr><w:b/><w:sz w:val='32'/></w:rPr><w:t xml:space='preserve'>$escaped</w:t></w:r></w:p>"
    }

    if ($Heading2) {
        return "<w:p><w:pPr><w:spacing w:before='120' w:after='40'/></w:pPr><w:r><w:rPr><w:b/><w:sz w:val='26'/></w:rPr><w:t xml:space='preserve'>$escaped</w:t></w:r></w:p>"
    }

    return "<w:p><w:pPr><w:spacing w:after='60'/></w:pPr><w:r><w:rPr><w:sz w:val='22'/></w:rPr><w:t xml:space='preserve'>$escaped</w:t></w:r></w:p>"
}

$paragraphs = @()

$paragraphs += New-ParagraphXml -Text "Методичка по панели размещения объектов на планете" -Heading1
$paragraphs += New-ParagraphXml -Text "Проект: Planet5. Тема документа: как в проект была добавлена пользовательская панель выбора объектов, как загружаются ресурсы, как работает размещение объектов на сфере и как работает удаление."

$paragraphs += New-ParagraphXml -Text "1. Общая идея решения" -Heading2
$paragraphs += New-ParagraphXml -Text "Задача была разделена на две большие части. Первая часть — это интерфейсная панель в левом верхнем углу, через которую пользователь выбирает действие: поставить фабрику, поставить рудник или удалить уже поставленный объект. Вторая часть — это игровая логика, которая получает выбранный режим и реагирует на клик по сфере: либо создаёт объект на выбранной ячейке, либо удаляет объект, если активен режим удаления."
$paragraphs += New-ParagraphXml -Text "Главная идея архитектуры такая: визуальная часть панели живёт в классе HexSphereWidget, потому что это Qt-виджет, который уже отвечает за пользовательские события и за отображение OpenGL-сцены. А вся логика выбора режима, постановки, проверки занятости клетки и удаления объектов живёт в InputController, потому что именно он уже обрабатывает клики мыши по планете и взаимодействует с данными сцены."

$paragraphs += New-ParagraphXml -Text "2. Какие файлы были задействованы" -Heading2
$paragraphs += New-ParagraphXml -Text "Основная визуальная часть панели находится в файлах ui/HexSphereWidget.h и ui/HexSphereWidget.cpp. Там создаются Qt-элементы интерфейса, задаётся их внешний вид, анимация, размеры и обработчики нажатий."
$paragraphs += New-ParagraphXml -Text "Логика режимов и работа со сценой находятся в files controllers/InputController.h и controllers/InputController.cpp. Там описан перечислимый тип PlacementModel, хранящий текущий выбранный режим, а также методы setPlacementModel, placeBuildingOnCell, isCellOccupied, deleteEntityAtHit и isDeletableEntity."
$paragraphs += New-ParagraphXml -Text "Ресурсы превью для карточек лежат в каталоге resources/models. Для отображения в панели используются изображения resources/models/factory.jpg и resources/models/mine.jpg."
$paragraphs += New-ParagraphXml -Text "Сами 3D-модели уже существовали в проекте раньше и отрисовываются через meshId. Для фабрики используется строковый идентификатор factory, для рудника — mine. В рендерере по этим строкам выбирается, какую 3D-модель рисовать."

$paragraphs += New-ParagraphXml -Text "3. Как устроена панель в HexSphereWidget" -Heading2
$paragraphs += New-ParagraphXml -Text "HexSphereWidget наследуется от QOpenGLWidget. Это означает, что он одновременно является и окном, в котором рисуется 3D-сцена, и обычным Qt-виджетом, поверх которого можно размещать интерфейсные элементы."
$paragraphs += New-ParagraphXml -Text "В конструкторе HexSphereWidget создаётся панель placementPanel_. Это объект QFrame. Он используется как контейнер для всех элементов панели. У него задан фон, рамка и скругление через setStyleSheet, поэтому визуально это отдельный блок поверх сцены."
$paragraphs += New-ParagraphXml -Text "Внутри placementPanel_ создаётся горизонтальный layout QHBoxLayout. Он нужен для того, чтобы слева располагалась узкая полоска-кнопка раскрытия, а справа — блок карточек с действиями."
$paragraphs += New-ParagraphXml -Text "Слева создаётся placementToggleButton_. Это QToolButton. Он показывает текст Build и стрелку. При нажатии на него вызывается метод setPlacementPanelExpanded, который анимирует горизонтальное сворачивание и разворачивание панели."
$paragraphs += New-ParagraphXml -Text "Справа создаётся placementContent_. Это ещё один QFrame, внутри которого лежат сами карточки. Для него создаётся ещё один горизонтальный layout contentLayout. Именно в него добавляются три карточки: Factory, Mine и Delete."

$paragraphs += New-ParagraphXml -Text "4. Какие Qt-элементы использованы и зачем" -Heading2
$paragraphs += New-ParagraphXml -Text "QFrame используется как контейнер. Первый QFrame — это вся панель, второй QFrame — это внутренняя область карточек."
$paragraphs += New-ParagraphXml -Text "QToolButton используется как интерактивная карточка. Этот класс удобен тем, что может одновременно показывать иконку и текст, а также работать в режиме checked или unchecked. Благодаря этому карточка может визуально подсвечиваться, когда инструмент выбран."
$paragraphs += New-ParagraphXml -Text "QHBoxLayout используется для горизонтального расположения элементов. Сначала для всей панели, затем для ряда карточек."
$paragraphs += New-ParagraphXml -Text "QVariantAnimation используется для плавного изменения ширины панели и ширины внутреннего блока с карточками. Это даёт эффект живого сворачивания панели по горизонтали."
$paragraphs += New-ParagraphXml -Text "QLabel используется отдельно для HUD-подсказки. Через него пользователю показываются текстовые сообщения: какой режим активирован, поставлен ли объект, можно ли удалить объект и так далее."
$paragraphs += New-ParagraphXml -Text "QPixmap и QIcon используются для загрузки изображений предпросмотра. Сначала картинка читается как QPixmap, затем заворачивается в QIcon и устанавливается в кнопку-карточку."

$paragraphs += New-ParagraphXml -Text "5. Как загружаются ресурсы карточек" -Heading2
$paragraphs += New-ParagraphXml -Text "Внутри конструктора HexSphereWidget есть лямбда-функция configurePlacementButton. Она принимает кнопку, текст и путь к изображению."
$paragraphs += New-ParagraphXml -Text "Внутри configurePlacementButton кнопке задаются: текст, режим checkable, стиль показа ToolButtonTextUnderIcon, размер иконки и фиксированный размер всей карточки."
$paragraphs += New-ParagraphXml -Text "Дальше Qt создаёт объект QPixmap по пути к изображению, например из файла resources/models/factory.jpg. Это и есть момент загрузки картинки предпросмотра из ресурсов проекта."
$paragraphs += New-ParagraphXml -Text "Если изображение считалось успешно, оно преобразуется в иконку и назначается кнопке-карточке. Благодаря этому на карточке появляется маленькое изображение модели."
$paragraphs += New-ParagraphXml -Text "Factory и Mine используют реальные jpg-файлы, а Delete сделана как текстовая карточка без отдельной картинки. Это допустимо, потому что Delete — не модель, а инструмент."

$paragraphs += New-ParagraphXml -Text "6. Как работает сворачивание и разворачивание панели" -Heading2
$paragraphs += New-ParagraphXml -Text "Панель можно представить как два вложенных прямоугольника. Первый прямоугольник — это placementPanel_, то есть весь внешний контейнер. Второй прямоугольник — это placementContent_, то есть область, где лежат карточки."
$paragraphs += New-ParagraphXml -Text "Когда панель раскрыта, placementPanel_ имеет ширину placementPanelExpandedWidth_, а placementContent_ имеет ширину placementContentExpandedWidth_. Эти размеры вычисляются после создания карточек."
$paragraphs += New-ParagraphXml -Text "Когда панель сворачивается, placementContent_ анимированно сжимается до нулевой ширины, а placementPanel_ — до placementPanelCollapsedWidth_. По сути остаётся только узкая левая полоска Build."
$paragraphs += New-ParagraphXml -Text "Для этого используются две анимации QVariantAnimation. Одна анимация меняет fixedWidth у всей панели. Вторая — fixedWidth у области карточек. По сигналу valueChanged на каждом промежуточном шаге новая ширина применяется к соответствующему виджету."
$paragraphs += New-ParagraphXml -Text "Метод setPlacementPanelExpanded принимает два параметра: expanded и animated. Если animated равно true, ширина изменяется плавно. Если false, нужное состояние выставляется сразу, например при начальной инициализации."

$paragraphs += New-ParagraphXml -Text "7. Как панель понимает, какая карточка выбрана" -Heading2
$paragraphs += New-ParagraphXml -Text "В InputController объявлено перечисление PlacementModel. Оно описывает все инструменты панели: None, Factory, Mine и Delete."
$paragraphs += New-ParagraphXml -Text "Текущее выбранное состояние хранится в поле placementModel_. Когда пользователь нажимает карточку, из HexSphereWidget вызывается togglePlacementSelection."
$paragraphs += New-ParagraphXml -Text "Метод togglePlacementSelection сравнивает: если пользователь нажал на уже выбранный инструмент, то режим снимается и устанавливается None. Если нажал на другой инструмент, он становится активным. Затем новый режим передаётся в InputController."
$paragraphs += New-ParagraphXml -Text "После этого syncPlacementPanelState проходит по всем карточкам и выставляет им флаги checked или unchecked. Благодаря этому активная карточка подсвечивается."

$paragraphs += New-ParagraphXml -Text "8. Как режим панели попадает в игровую логику" -Heading2
$paragraphs += New-ParagraphXml -Text "Класс HexSphereWidget не создаёт объекты на сфере напрямую. Он только меняет выбранный режим в InputController. Это важный архитектурный момент: UI не лезет в данные сцены сам, а сообщает контроллеру, что теперь пользователь работает, например, в режиме Factory."
$paragraphs += New-ParagraphXml -Text "Метод InputController::setPlacementModel сохраняет новый режим в поле placementModel_. Одновременно он формирует текстовую подсказку для пользователя. Например, для Factory показывается сообщение, что режим постановки фабрики включён, а для Delete — что можно кликнуть по фабрике или руднику и удалить их."

$paragraphs += New-ParagraphXml -Text "9. Как работает клик по сфере" -Heading2
$paragraphs += New-ParagraphXml -Text "Центральная точка входа — метод InputController::mousePress. Он вызывается каждый раз, когда пользователь нажимает кнопку мыши в окне."
$paragraphs += New-ParagraphXml -Text "Если нажата правая кнопка мыши, начинается вращение камеры. Это отдельный режим и к панели не относится."
$paragraphs += New-ParagraphXml -Text "Если нажата левая кнопка мыши, контроллер сначала определяет, куда именно попал клик. Для этого вызывается pickSceneAt."
$paragraphs += New-ParagraphXml -Text "pickSceneAt объединяет два вида проверки. Сначала проверяется попадание в сущности через pickEntityAt. Потом проверяется попадание в поверхность планеты через pickTerrainAt. Если попадание есть и туда и туда, выбирается более близкий по расстоянию результат."
$paragraphs += New-ParagraphXml -Text "Результат хранится в структуре PickHit. В ней есть: cellId, entityId, координата точки попадания, расстояние t и флаг isEntity, который говорит, кликнули ли мы в объект или в саму поверхность."

$paragraphs += New-ParagraphXml -Text "10. Как происходит постановка фабрики или рудника" -Heading2
$paragraphs += New-ParagraphXml -Text "Если активен режим Factory или Mine, в mousePress срабатывает ветка isPlacementModeActive. Если клик попал в уже существующий объект, контроллер пишет сообщение, что выбранная клетка занята. Если клик попал в свободную клетку поверхности, вызывается placeBuildingOnCell."
$paragraphs += New-ParagraphXml -Text "В начале placeBuildingOnCell выполняется проверка корректности cellId. Затем вызывается isCellOccupied. Этот метод проходит по всем сущностям ECS и смотрит, есть ли уже объект с таким currentCell. Если да, создание запрещается."
$paragraphs += New-ParagraphXml -Text "Если клетка свободна, создаётся новая сущность через ecs_.createEntity. Имя сущности выбирается по placementModelName. После этого в ECS добавляется компонент Mesh с meshId равным factory или mine."
$paragraphs += New-ParagraphXml -Text "Затем добавляется компонент Transform. В него записывается позиция, вычисленная специальной функцией проекта для поиска координаты на поверхности сферы по номеру выбранной ячейки. То есть объект ставится не приблизительно, а точно в нужную клетку планеты."
$paragraphs += New-ParagraphXml -Text "После этого добавляется компонент Collider с радиусом 0.16. Он нужен, чтобы потом по объекту можно было кликать и чтобы система pickEntityAt могла определить попадание луча в объект."
$paragraphs += New-ParagraphXml -Text "В конце пользователю показывается сообщение вида Factory placed on cell N или Mine placed on cell N."

$paragraphs += New-ParagraphXml -Text "11. Почему после постановки объект нельзя двигать" -Heading2
$paragraphs += New-ParagraphXml -Text "Запрет на перемещение сделан логически, а не через блокировку интерфейса. В проекте уже существовала функция isMovableEntity. Она проверяет компонент Mesh у сущности."
$paragraphs += New-ParagraphXml -Text "Если meshId равен factory или mine, функция возвращает false. Это означает, что фабрика и рудник считаются статичными строениями."
$paragraphs += New-ParagraphXml -Text "Когда пользователь пытается переместить выбранную сущность через уже существующую механику moveSelectedEntityToCell, в начале метода вызывается isMovableEntity. Если объект статичный, перемещение сразу отменяется и выводится сообщение Static building cannot be moved."
$paragraphs += New-ParagraphXml -Text "То есть запрет на перемещение интегрирован в старую систему перемещения и не требует отдельного костыля специально для панели."

$paragraphs += New-ParagraphXml -Text "12. Как работает удаление через карточку Delete" -Heading2
$paragraphs += New-ParagraphXml -Text "Delete реализована не как отдельная кнопка вне системы, а как такой же PlacementModel, как Factory и Mine. Это удобно, потому что UI работает по одной и той же схеме: пользователь выбирает инструмент, затем кликает по сцене."
$paragraphs += New-ParagraphXml -Text "Если активен режим Delete, mousePress не вызывает placeBuildingOnCell, а вызывает deleteEntityAtHit."
$paragraphs += New-ParagraphXml -Text "Метод deleteEntityAtHit анализирует объект PickHit. Если клик попал прямо в сущность, берётся entityId из hit. Если клик попал в клетку поверхности, дополнительно выполняется поиск сущности, которая стоит на этой клетке."
$paragraphs += New-ParagraphXml -Text "Дальше вызывается isDeletableEntity. Этот метод проверяет meshId сущности и разрешает удалять только factory и mine. Машинка с meshId car не удаляется этим режимом."
$paragraphs += New-ParagraphXml -Text "Если найден допустимый объект, он удаляется из ECS. Если этот объект был выделен, выделение сначала снимается. После этого пользователю показывается сообщение о том, что строение удалено."

$paragraphs += New-ParagraphXml -Text "13. Как определяется попадание мыши в объект или ячейку" -Heading2
$paragraphs += New-ParagraphXml -Text "Это важная часть объяснения преподавателю, потому что именно она связывает 2D-клик мыши с 3D-миром."
$paragraphs += New-ParagraphXml -Text "Для поверхности планеты используется pickTerrainAt. Сначала из камеры берутся начало луча rayOrigin и направление rayDirectionFromScreen. Потом этот луч проверяется на пересечение со всеми треугольниками terrain mesh. Для проверки пересечения используется функция rayTriangleMT, то есть алгоритм Мёллера — Трумбора."
$paragraphs += New-ParagraphXml -Text "Для сущностей используется pickEntityAt. Здесь каждый объект рассматривается как сфера коллизии с центром в transform.position и радиусом collider.radius. Луч пересекается с этой сферой, и если пересечение существует, объект считается выбранным."
$paragraphs += New-ParagraphXml -Text "После этого pickSceneAt сравнивает оба результата и выбирает ближайший к камере. Поэтому если пользователь кликает по модели, а не по земле за ней, система обычно выбирает именно модель."

$paragraphs += New-ParagraphXml -Text "14. Как новые объекты потом отображаются в рендерере" -Heading2
$paragraphs += New-ParagraphXml -Text "Панель сама ничего не рисует в 3D. Она только создаёт сущности в ECS и задаёт им meshId."
$paragraphs += New-ParagraphXml -Text "Когда происходит очередной кадр, InputController::render передаёт в HexSphereRenderer граф сцены, в котором уже есть ECS с новыми сущностями."
$paragraphs += New-ParagraphXml -Text "Дальше EntityRenderer проходит по сущностям с компонентами Mesh и Transform. Если meshId == factory, вызывается renderFactory. Если meshId == mine, вызывается renderMine. Таким образом одна и та же уже существующая 3D-логика автоматически начинает рисовать новые поставленные пользователем объекты."

$paragraphs += New-ParagraphXml -Text "15. Почему решение удобно с архитектурной точки зрения" -Heading2
$paragraphs += New-ParagraphXml -Text "Плюс решения в том, что UI и игровая логика разделены. HexSphereWidget отвечает за создание кнопок, анимацию панели и визуальную обратную связь. InputController отвечает за поведение сцены и за работу с данными."
$paragraphs += New-ParagraphXml -Text "Это значит, что в будущем можно добавить новые карточки по той же схеме. Например, можно добавить карточку Warehouse или карточку Road, просто расширив PlacementModel, добавив ещё одну кнопку в панели и прописав новую логику в контроллере."
$paragraphs += New-ParagraphXml -Text "Также решение хорошо тем, что не ломает старую архитектуру. Используются уже существующие ECS, уже существующий pick по сцене и уже существующий рендерер. Новая панель просто подключается к этим системам."

$paragraphs += New-ParagraphXml -Text "16. Короткий алгоритм, как рассказать преподавателю устно" -Heading2
$paragraphs += New-ParagraphXml -Text "Шаг 1. Я добавил в HexSphereWidget визуальную панель поверх QOpenGLWidget, потому что этот класс уже отвечает за окно сцены и умеет содержать обычные Qt-элементы."
$paragraphs += New-ParagraphXml -Text "Шаг 2. В панели я создал карточки на QToolButton: Factory, Mine и Delete. Для Factory и Mine я загрузил превью через QPixmap из resources/models/factory.jpg и resources/models/mine.jpg."
$paragraphs += New-ParagraphXml -Text "Шаг 3. Я добавил в InputController перечисление PlacementModel, чтобы хранить текущий выбранный инструмент."
$paragraphs += New-ParagraphXml -Text "Шаг 4. При нажатии на карточку панель вызывает setPlacementModel в контроллере, то есть UI только сообщает, какой режим теперь активен."
$paragraphs += New-ParagraphXml -Text "Шаг 5. При клике мышью по сцене контроллер делает pickSceneAt и получает, куда именно пользователь нажал: в объект или в ячейку поверхности."
$paragraphs += New-ParagraphXml -Text "Шаг 6. Если активен Factory или Mine, контроллер проверяет, свободна ли ячейка, и создаёт сущность в ECS с нужным meshId и Transform на поверхности сферы."
$paragraphs += New-ParagraphXml -Text "Шаг 7. Если активен Delete, контроллер ищет фабрику или рудник под курсором и удаляет сущность через ECS."
$paragraphs += New-ParagraphXml -Text "Шаг 8. Отрисовка новых объектов происходит автоматически через уже существующий EntityRenderer, потому что он и так умеет рисовать сущности с meshId factory и mine."

$paragraphs += New-ParagraphXml -Text "17. Итог" -Heading2
$paragraphs += New-ParagraphXml -Text "В результате была добавлена полноценная пользовательская панель для работы с объектами на планете. Пользователь может открыть панель, выбрать тип действия, кликнуть по ячейке сферы и получить ожидаемое поведение: установка фабрики, установка рудника или удаление здания. Внутри проекта это реализовано через связку Qt UI + InputController + ECS + существующий 3D-рендерер."

$documentXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
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
    $($paragraphs -join "`r`n    ")
    <w:sectPr>
      <w:pgSz w:w="11906" w:h="16838"/>
      <w:pgMar w:top="1134" w:right="1134" w:bottom="1134" w:left="1134" w:header="708" w:footer="708" w:gutter="0"/>
    </w:sectPr>
  </w:body>
</w:document>
"@

$contentTypesXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>
"@

$relsXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>
"@

if (Test-Path $outputPath) {
    Remove-Item $outputPath -Force
}

$zip = [System.IO.Compression.ZipFile]::Open($outputPath, [System.IO.Compression.ZipArchiveMode]::Create)

try {
    $entry = $zip.CreateEntry("[Content_Types].xml")
    $writer = New-Object System.IO.StreamWriter($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    $writer.Write($contentTypesXml)
    $writer.Dispose()

    $entry = $zip.CreateEntry("_rels/.rels")
    $writer = New-Object System.IO.StreamWriter($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    $writer.Write($relsXml)
    $writer.Dispose()

    $entry = $zip.CreateEntry("word/document.xml")
    $writer = New-Object System.IO.StreamWriter($entry.Open(), [System.Text.UTF8Encoding]::new($false))
    $writer.Write($documentXml)
    $writer.Dispose()
}
finally {
    $zip.Dispose()
}

Write-Output $outputPath
