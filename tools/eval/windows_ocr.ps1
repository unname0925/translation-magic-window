# M0-11：用 Windows 內建的 OCR（Windows.Media.Ocr）辨識真實截圖，輸出格式和 tmw_ocr_cli 相同，
# 給 evaluate_ocr.py 評分。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/eval/windows_ocr.ps1 -Category en-web
#
# 輸出 build/ocr_eval/m0-11/raw/winrt/windows-ocr/<分類>.json。需要對應語言的 OCR 語言套件
# （Windows 設定 → 時間與語言 → 語言 → 選用功能）；目前安裝的語言可以用 -List 查看。
# Windows OCR 沒有信心分數，score 一律是 1。每張截圖辨識 2 次，耗時取第二次。
# 這個檔案要存成有 BOM 的 UTF-8：Windows PowerShell 5.1 會把沒有 BOM 的檔案當成系統的字碼頁讀取。

param(
    [string]$Category,
    [switch]$List
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime] | Out-Null
[Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime] | Out-Null

if ($List) {
    [Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages | Select-Object LanguageTag, DisplayName
    return
}

# WinRT 的非同步呼叫轉成 .NET 的 Task 再等待
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq "AsTask" -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
function Await($operation, [Type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
    $task.Wait(-1) | Out-Null
    return $task.Result
}

$repo = Resolve-Path (Join-Path $PSScriptRoot "../..")
$folder = Join-Path $repo "testdata/private/$Category"
$languageTag = @{ "ja" = "ja-JP"; "en" = "en-US"; "ko" = "ko-KR" }[$Category.Split("-")[0]]
$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage([Windows.Globalization.Language]::new($languageTag))
if ($null -eq $engine) {
    throw "沒有安裝 $languageTag 的 OCR 語言套件（用 -List 查看已安裝的語言）"
}

$images = @()
Get-ChildItem $folder -File | Where-Object { $_.Extension -in ".png", ".jpg", ".jpeg", ".webp" } |
    Sort-Object Name | ForEach-Object {
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($_.FullName)) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult]) | Out-Null  # 暖機
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    $milliseconds = $watch.Elapsed.TotalMilliseconds
    $lines = @($result.Lines | ForEach-Object {
        $rects = $_.Words | ForEach-Object { $_.BoundingRect }
        $x0 = ($rects | Measure-Object -Property X -Minimum).Minimum
        $y0 = ($rects | Measure-Object -Property Y -Minimum).Minimum
        $x1 = ($rects | ForEach-Object { $_.X + $_.Width } | Measure-Object -Maximum).Maximum
        $y1 = ($rects | ForEach-Object { $_.Y + $_.Height } | Measure-Object -Maximum).Maximum
        [ordered]@{ text = $_.Text; score = 1.0; box = @(@($x0, $y0), @($x1, $y0), @($x1, $y1), @($x0, $y1)) }
    })
    $images += [ordered]@{
        image = $_.Name; width = $bitmap.PixelWidth; height = $bitmap.PixelHeight; lines = $lines
        timings_ms = [ordered]@{ detection = 0; recognition = $milliseconds }
    }
    $stream.Dispose()
    Write-Host ("{0}: {1} lines, {2:N0} ms" -f $_.Name, $lines.Count, $milliseconds)
}

$output = Join-Path $repo "build/ocr_eval/m0-11/raw/winrt/windows-ocr/$Category.json"
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
$json = [ordered]@{ implementation = "Windows.Media.Ocr"; language = $languageTag; images = $images } |
    ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($output, $json, (New-Object System.Text.UTF8Encoding $false))
Write-Host "wrote $output"
