<#
 ============================================================================
 Android 桌面图标生成
 ============================================================================
 从根目录 axt-app-icon.png 生成 5 档 mipmap：
   mdpi 48、hdpi 72、xhdpi 96、xxhdpi 144、xxxhdpi 192

 每个尺寸同时生成：
   ic_launcher.png        保留原图透明圆角
   ic_launcher_round.png  圆形裁切

 用法：
   powershell -ExecutionPolicy Bypass -File tools\build-app-icons.ps1 -DryRun
   powershell -ExecutionPolicy Bypass -File tools\build-app-icons.ps1
 ============================================================================
#>

param(
    [string]$Source = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Join-Path $projectRoot 'axt-app-icon.png'
}
$Source = [System.IO.Path]::GetFullPath($Source)
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "找不到图标母版：$Source"
}

Add-Type -AssemblyName System.Drawing
$sourceImage = [System.Drawing.Image]::FromFile($Source)
try {
    if ($sourceImage.Width -ne $sourceImage.Height) {
        throw "图标母版必须是正方形：$($sourceImage.Width)x$($sourceImage.Height)"
    }
    if (-not [System.Drawing.Image]::IsAlphaPixelFormat($sourceImage.PixelFormat)) {
        throw "图标母版没有透明通道：$($sourceImage.PixelFormat)"
    }

    $targets = @(
        @{ Density = 'mdpi'; Size = 48 },
        @{ Density = 'hdpi'; Size = 72 },
        @{ Density = 'xhdpi'; Size = 96 },
        @{ Density = 'xxhdpi'; Size = 144 },
        @{ Density = 'xxxhdpi'; Size = 192 }
    )

    Write-Host "母版：$Source ($($sourceImage.Width)x$($sourceImage.Height))"
    foreach ($target in $targets) {
        $density = $target.Density
        $size = [int]$target.Size
        $resDir = Join-Path $projectRoot "app\src\main\res\mipmap-$density"
        New-Item -ItemType Directory -Path $resDir -Force | Out-Null

        foreach ($round in @($false, $true)) {
            $name = if ($round) { 'ic_launcher_round.png' } else { 'ic_launcher.png' }
            $outPath = Join-Path $resDir $name
            if ($DryRun) {
                Write-Host "  将生成：$outPath（$size x $size）"
                continue
            }

            $canvas = [System.Drawing.Bitmap]::new(
                $size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [System.Drawing.Graphics]::FromImage($canvas)
            $clipPath = $null
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality =
                    [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode =
                    [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode =
                    [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode =
                    [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

                if ($round) {
                    $clipPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
                    $clipPath.AddEllipse(0, 0, $size, $size)
                    $graphics.SetClip($clipPath, [System.Drawing.Drawing2D.CombineMode]::Replace)
                }

                $dest = [System.Drawing.Rectangle]::new(0, 0, $size, $size)
                $graphics.DrawImage(
                    $sourceImage, $dest, 0, 0, $sourceImage.Width, $sourceImage.Height,
                    [System.Drawing.GraphicsUnit]::Pixel)
            } finally {
                if ($null -ne $clipPath) {
                    $clipPath.Dispose()
                }
                $graphics.Dispose()
            }

            $tempPath = $outPath + '.tmp'
            try {
                $canvas.Save($tempPath, [System.Drawing.Imaging.ImageFormat]::Png)
            } finally {
                $canvas.Dispose()
            }
            Move-Item -LiteralPath $tempPath -Destination $outPath -Force
            Write-Host "  已生成：$outPath" -ForegroundColor Green
        }
    }
} finally {
    $sourceImage.Dispose()
}

if ($DryRun) {
    Write-Host 'DryRun：未写入图标文件。' -ForegroundColor Yellow
} else {
    Write-Host 'Android 桌面图标已全部更新。' -ForegroundColor Green
}
