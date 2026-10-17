$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$assetDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'src\assets'
New-Item -ItemType Directory -Path $assetDir -Force | Out-Null

function RoundedPath([float]$x, [float]$y, [float]$width, [float]$height, [float]$radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc(($x+$width-$d), $y, $d, $d, 270, 90)
    $path.AddArc(($x+$width-$d), ($y+$height-$d), $d, $d, 0, 90)
    $path.AddArc($x, ($y+$height-$d), $d, $d, 90, 90)
    $path.CloseFigure()
    return ,$path
}

$frames = @()
foreach ($size in @(16, 20, 24, 32, 48, 64, 128, 256)) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = 'AntiAlias'
    $graphics.ScaleTransform(($size / 256.0), ($size / 256.0))
    $background = RoundedPath 8 8 240 240 52
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush([System.Drawing.Point]::new(0,0), [System.Drawing.Point]::new(256,256), [System.Drawing.ColorTranslator]::FromHtml('#24374D'), [System.Drawing.ColorTranslator]::FromHtml('#101927'))
    $graphics.FillPath($gradient, $background)
    $backPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml('#69D9EB'), 12)
    $frontPen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml('#F3F8FC'), 12)
    $closePen = New-Object System.Drawing.Pen([System.Drawing.ColorTranslator]::FromHtml('#69D9EB'), 14)
    $closePen.StartCap = 'Round'; $closePen.EndCap = 'Round'
    $back = RoundedPath 56 51 137 115 14
    $front = RoundedPath 43 81 153 124 14
    $graphics.DrawPath($backPen, $back)
    $fill = New-Object System.Drawing.SolidBrush([System.Drawing.ColorTranslator]::FromHtml('#182638'))
    $graphics.FillPath($fill, $front)
    $graphics.DrawPath($frontPen, $front)
    $graphics.DrawLine($frontPen, 47, 113, 192, 113)
    $graphics.DrawLine($closePen, 100, 141, 139, 180)
    $graphics.DrawLine($closePen, 139, 141, 100, 180)
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += [pscustomobject]@{ Size=$size; Bytes=$stream.ToArray() }
    if ($size -eq 256) { $bitmap.Save((Join-Path $assetDir 'app.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
    $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
    $background.Dispose(); $gradient.Dispose(); $backPen.Dispose(); $frontPen.Dispose(); $closePen.Dispose(); $back.Dispose(); $front.Dispose(); $fill.Dispose()
}
$output = [System.IO.File]::Create((Join-Path $assetDir 'app.ico'))
$writer = New-Object System.IO.BinaryWriter($output)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($frame in $frames) {
    $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$frame.Bytes.Length); $writer.Write([uint32]$offset)
    $offset += $frame.Bytes.Length
}
foreach ($frame in $frames) { $writer.Write([byte[]]$frame.Bytes) }
$writer.Dispose()
