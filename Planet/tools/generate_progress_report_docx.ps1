$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Escape-Xml {
    param([string]$Text)

    if ($null -eq $Text) {
        return ""
    }

    return $Text.Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;")
}

function New-RunXml {
    param(
        [string]$Text,
        [switch]$Bold
    )

    $escaped = Escape-Xml $Text
    $props = ""
    if ($Bold) {
        $props = "<w:rPr><w:b/></w:rPr>"
    }

    return "<w:r>$props<w:t xml:space=`"preserve`">$escaped</w:t></w:r>"
}

function New-ParagraphXml {
    param(
        [string[]]$Runs,
        [switch]$SpacingAfter
    )

    $spacing = ""
    if ($SpacingAfter) {
        $spacing = "<w:pPr><w:spacing w:after=`"160`"/></w:pPr>"
    }

    return "<w:p>$spacing$($Runs -join '')</w:p>"
}

$root = Split-Path -Parent $PSScriptRoot
$outPath = Join-Path $root "progress_report_dag_coursework.docx"
$workDir = Join-Path $env:TEMP ("dag_report_" + [guid]::NewGuid().ToString("N"))
$wordDir = Join-Path $workDir "word"
$relsDir = Join-Path $workDir "_rels"

New-Item -ItemType Directory -Path $wordDir -Force | Out-Null
New-Item -ItemType Directory -Path $relsDir -Force | Out-Null

$paragraphs = @()

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Отчет о текущем состоянии проекта ""Разработка вычислительной модели на основе направленного ациклического графа""" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "В данном документе приводится описание выполненных работ по проекту, связанных с переносом вычислительных подсистем в архитектуру DAG, восстановлением пользовательских команд, а также подготовкой средств тестирования и анализа производительности.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "1. Постановка текущего этапа работ" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "На текущем этапе ставилась задача развить уже существующий переход на направленный ациклический граф вычислений и расширить его на дополнительные части сцены и логики проекта. Одновременно требовалось восстановить корректную работу команд пользовательского интерфейса, которые ранее вызывались через клавиатуру и меню.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Основные практические цели этапа:")
) -SpacingAfter

$bulletItems = @(
    "унифицировать вызов команд сцены и исправить работу переключателей и клавиш;",
    "обеспечить сохранение ручных правок клеток без повторного полного запуска генератора;",
    "перенести вычисление части производных сценовых данных в DAG;",
    "добавить guard-флаги и кэширование для сокращения лишних пересчетов;",
    "подготовить benchmark-сценарии для сравнения DAG и Legacy-подходов."
)

foreach ($item in $bulletItems) {
    $paragraphs += New-ParagraphXml -Runs @(
        (New-RunXml -Text ("• " + $item))
    )
}

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "2. Выполненные изменения в пользовательских командах" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "В проекте был введен единый слой команд сцены. Это позволило связать одни и те же действия с клавиатурой, меню и toolbar, не дублируя логику обработки. Команды +, -, S, 1-8, P, C, W, O и дополнительные действия выбора теперь проходят через общий механизм исполнения.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Дополнительно была исправлена проблема, при которой ручные изменения высоты или биома клеток могли теряться при повторной сборке сцены. Для этого полный пересчет мира был отделен от пересчета производной геометрии. Теперь после локальных правок пересобираются только зависимые данные: сетка, вода, контуры выбора, деревья и другие derived-представления.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Также была устранена регрессия с подсветкой выбранной клетки. Если DAG-слой временно не возвращает валидный контур выделения, используется безопасный fallback на локальную генерацию outline, благодаря чему визуальная подсветка пользователя не исчезает.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "3. Перенос вычислений сцены в DAG" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Для производных сценовых данных был добавлен отдельный backend DAG сцены. В его состав вошли узлы, формирующие контур выделения клеток, размещение деревьев и вычисление размещения моделей или сущностей на поверхности сферы.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Важно отметить, что в DAG были перенесены именно вычислительные представления, а не низкоуровневая графическая загрузка ресурсов. Загрузка OBJ-моделей, текстур и OpenGL-буферов по-прежнему остается в слое рендеринга, а DAG возвращает только вычисленные снапшоты и дескрипторы размещения.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Интеграция с фасадом движка позволила вызывать сценовый DAG при обновлении selection outline и других зависимых данных, а также выводить диагностические показатели работы DAG в overlay интерфейса.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "4. Guard-флаги и кэширование" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Для сценового DAG были реализованы guard-флаги dirty-типа. Они определяют, требуется ли конкретному узлу участвовать в следующем цикле вычислений. Если входные данные не изменились, узел может быть пропущен без выполнения.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "После успешного выполнения узел сбрасывает собственный dirty-флаг. Благодаря этому повторные запросы с теми же входами не запускают лишний пересчет. Кроме того, был добавлен кэш результатов для selection outline, tree placements и model placements. При возврате к уже встречавшемуся состоянию вычисленный результат может быть извлечен из кэша.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Диагностически отслеживаются следующие величины: количество выполненных узлов, количество узлов, пропущенных guard-механизмом, число попаданий в кэш и число промахов кэша.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "5. Benchmark и тестирование" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Для проекта подготовлен benchmark, сравнивающий DAG и Legacy-подходы. Benchmark сохраняет результаты в CSV-файл и поддерживает как сценарии полной регенерации terrain, так и сценарии вычисления производных данных сцены.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "В тестовых сценариях terrain сравниваются одинаковые параметры генерации, чтобы проверить совместимость снимков данных и оценить абсолютное время полной регенерации. В сценовых сценариях добавлены операции baseline, repeat_same, selection_change, selection_revert, visual_change, visual_revert, terrain_edit и terrain_revert. Это позволяет увидеть не только чистое время, но и эффект selective recomputation, guard-пропуска и cache reuse.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Результаты benchmark показывают, что на полной регенерации terrain DAG пока уступает Legacy-подходу по времени выполнения. Однако на ряде инкрементальных сценовых операций DAG уже демонстрирует преимущество, особенно при повторных запросах и возврате к ранее вычисленному состоянию.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "С практической точки зрения это означает, что сильная сторона DAG проявляется не в полном пересчете всего мира, а в организации частичных пересчетов, когда изменяется только часть входов и существует возможность пропустить или повторно использовать результаты отдельных узлов.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "6. Дополнительные материалы" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "Для удобства дальнейшего анализа был также подготовлен Python-скрипт, который по benchmark CSV строит графики сравнения DAG и Legacy-подходов. Эти графики могут быть использованы в следующих версиях отчета после выбора окончательного стиля оформления.")
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "7. Итоги текущего этапа" -Bold)
) -SpacingAfter

$paragraphs += New-ParagraphXml -Runs @(
    (New-RunXml -Text "В результате выполненного этапа проект получил рабочий базовый слой сценового DAG, исправленную систему пользовательских команд, механизм guard-флагов, кэширование производных сценовых вычислений и инструмент benchmark-анализа. Создана основа для следующего этапа, на котором можно будет расширять перенос вычислительных подсистем в DAG, углублять тестирование и оформлять итоговый текст курсовой работы.")
) -SpacingAfter

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
    $($paragraphs -join "`n    ")
    <w:sectPr>
      <w:pgSz w:w="11906" w:h="16838"/>
      <w:pgMar w:top="1417" w:right="1134" w:bottom="1417" w:left="1701" w:header="708" w:footer="708" w:gutter="0"/>
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

Set-Content -Path (Join-Path $workDir "[Content_Types].xml") -Value $contentTypesXml -Encoding UTF8
Set-Content -Path (Join-Path $relsDir ".rels") -Value $relsXml -Encoding UTF8
Set-Content -Path (Join-Path $wordDir "document.xml") -Value $documentXml -Encoding UTF8

$zipPath = [System.IO.Path]::ChangeExtension($outPath, ".zip")
if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
if (Test-Path $outPath) {
    Remove-Item -LiteralPath $outPath -Force
}

[System.IO.Compression.ZipFile]::CreateFromDirectory($workDir, $zipPath)
Move-Item -LiteralPath $zipPath -Destination $outPath
Remove-Item -LiteralPath $workDir -Recurse -Force

Write-Output $outPath
