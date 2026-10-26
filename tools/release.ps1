<#
 ============================================================================
 发布包生成（Windows Release + Android Release + 校验值）
 ============================================================================
 做的事：
   1. 校验 Version.h、Gradle versionName 与可选 -ExpectedVersion 一致
   2. 默认要求 Git 工作区干净（仅构建产物除外）
   3. Windows：Release 构建并运行完整测试，生成便携 ZIP
   4. Android：调用现有 build-android.ps1 -Release
   5. 在 dist\ 生成 SHA-256 清单和发布说明

 用法：
   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -DryRun -AllowDirty
   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -ExpectedVersion 0.2.3
   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -SkipAndroid
   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -SkipWindows -SkipAndroid  # 会报错

 产物：
   dist\AxtUebung-<version>.apk
   dist\AxtUebung-<version>-windows-x64.zip
   dist\AxtUebung-<version>-SHA256SUMS.txt
   dist\AxtUebung-<version>-RELEASE.md

 注意：
   * 当前 Android release 仍用开发调试密钥签名，只适合侧载测试。
   * 发布 ZIP 不包含 data\、work\、*.pdb、*.ilk 或 assets 里的 *.bak-* 文件。
 ============================================================================
#>

param(
    [string]$ExpectedVersion = '',
    [switch]$SkipWindows,
    [switch]$SkipAndroid,
    [switch]$AllowDirty,
    [switch]$Force,
    [switch]$KillRunning,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Invoke-GitText {
    param([string[]]$Arguments, [switch]$AllowFailure)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git -C $projectRoot @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($exitCode -eq 0) {
        $output = @($output | Where-Object { "$_" -notmatch '(?i)warning:' })
    }
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw ("git {0} 失败：{1}" -f ($Arguments -join ' '), ($output -join "`n"))
    }
    return $output
}

function Assert-UnderProject {
    param([string]$Path)
    $root = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\', '/')
    $full = [System.IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar,
                               [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝操作项目目录之外的路径：$full"
    }
}

function Copy-RequiredFile {
    param([string]$Source, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "缺少发布文件：$Source"
    }
    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-TreeFiltered {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExcludeNames = @(),
        [string[]]$ExcludeRelativePatterns = @()
    )
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "缺少发布目录：$Source"
    }
    $sourceFull = [System.IO.Path]::GetFullPath($Source).TrimEnd('\', '/')
    foreach ($file in Get-ChildItem -LiteralPath $Source -Recurse -File -Force) {
        $relative = $file.FullName.Substring($sourceFull.Length).TrimStart('\', '/')
        $relativeMatch = $relative.Replace('\', '/')
        $excluded = $false
        foreach ($pattern in $ExcludeNames) {
            if ($file.Name -like $pattern) {
                $excluded = $true
                break
            }
        }
        if (-not $excluded) {
            foreach ($pattern in $ExcludeRelativePatterns) {
                if ($relativeMatch -like $pattern) {
                    $excluded = $true
                    break
                }
            }
        }
        if ($excluded) {
            continue
        }
        $target = Join-Path $Destination $relative
        $parent = Split-Path -Parent $target
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
}

function Read-SingleMatch {
    param([string]$Path, [string]$Pattern, [string]$Description)
    $text = Get-Content -LiteralPath $Path -Raw
    $match = [regex]::Match($text, $Pattern)
    if (-not $match.Success) {
        throw "读不到$Description：$Path"
    }
    return $match.Groups[1].Value.Trim()
}

if ($SkipWindows -and $SkipAndroid) {
    throw '不能同时跳过 Windows 和 Android。'
}

$versionHeaderPath = Join-Path $projectRoot 'core\include\quiz\app\Version.h'
$gradlePath = Join-Path $projectRoot 'app\build.gradle.kts'
$headerVersion = Read-SingleMatch -Path $versionHeaderPath `
    -Pattern 'kAppVersion\s*=\s*"([^"]+)"' -Description 'Version.h 版本号'
$gradleVersion = Read-SingleMatch -Path $gradlePath `
    -Pattern 'versionName\s*=\s*"([^"]+)"' -Description 'Gradle versionName'
$versionCode = Read-SingleMatch -Path $gradlePath `
    -Pattern 'versionCode\s*=\s*(\d+)' -Description 'Gradle versionCode'

if ($headerVersion -ne $gradleVersion) {
    throw "版本号不一致：Version.h = $headerVersion，Gradle = $gradleVersion"
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and
    $ExpectedVersion.TrimStart('v') -ne $gradleVersion) {
    throw "期望版本 $ExpectedVersion 与当前版本 $gradleVersion 不一致"
}
if ($gradleVersion -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    throw "版本号格式不适合发布：$gradleVersion"
}

$builtinBanks = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'assets\banks') `
    -File -Filter '*.quiz' -ErrorAction SilentlyContinue)
if ($builtinBanks.Count -gt 0) {
    throw ("发布前必须清空内置题库，当前仍有 {0} 个：`n{1}" -f
        $builtinBanks.Count, (($builtinBanks | Select-Object -ExpandProperty Name) -join "`n"))
}

$status = @(Invoke-GitText -Arguments @('status', '--porcelain'))
if ($status.Count -gt 0 -and -not $AllowDirty) {
    $preview = ($status | Select-Object -First 20) -join "`n"
    if ($status.Count -gt 20) {
        $preview += "`n..."
    }
    throw "工作区不干净，发布前请先提交或使用 -AllowDirty。`n$preview"
}

$commit = ''
$commitOutput = @(Invoke-GitText -Arguments @('rev-parse', '--short', 'HEAD') -AllowFailure)
if ($commitOutput.Count -gt 0) {
    $commit = $commitOutput[0]
}
$dirtyMark = if ($status.Count -gt 0) { 'dirty' } else { 'clean' }

$distDir = Join-Path $projectRoot 'dist'
$prefix = "AxtUebung-$gradleVersion"
$androidApk = Join-Path $distDir "$prefix.apk"
$windowsZip = Join-Path $distDir "$prefix-windows-x64.zip"
$sumsPath = Join-Path $distDir "$prefix-SHA256SUMS.txt"
$notesPath = Join-Path $distDir "$prefix-RELEASE.md"
$outputs = @($sumsPath, $notesPath)
if (-not $SkipWindows) {
    $outputs += $windowsZip
}
if (-not $SkipAndroid) {
    $outputs += $androidApk
}

Write-Host "版本        : $gradleVersion (versionCode $versionCode)"
Write-Host "提交        : $commit ($dirtyMark)"
Write-Host "Windows     : $(if ($SkipWindows) { '跳过' } else { 'Release + tests' })"
Write-Host "Android     : $(if ($SkipAndroid) { '跳过' } else { 'Release（调试签名）' })"
Write-Host "输出目录    : $distDir"

if ($DryRun) {
    Write-Host 'DryRun：不会构建、删除或写入文件。' -ForegroundColor Yellow
    if (-not $SkipWindows) {
        Write-Host "  将构建 Windows Release：build\win-release\bin\AxtQuiz.exe"
        Write-Host "  将生成：$windowsZip"
    }
    if (-not $SkipAndroid) {
        Write-Host "  将构建 Android Release：$androidApk"
    }
    Write-Host "  将生成：$sumsPath"
    Write-Host "  将生成：$notesPath"
    return
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
if (-not $Force) {
    $existing = @($outputs | Where-Object { Test-Path -LiteralPath $_ })
    if ($existing.Count -gt 0) {
        throw ("发布产物已存在，未覆盖：`n{0}`n需要覆盖时加 -Force。" -f ($existing -join "`n"))
    }
} else {
    foreach ($path in $outputs) {
        if (Test-Path -LiteralPath $path) {
            Assert-UnderProject $path
            Remove-Item -LiteralPath $path -Force
        }
    }
}

if (-not $SkipWindows) {
    $winBuildDir = 'build/win-release'
    $winBuildScript = Join-Path $PSScriptRoot 'build-windows.ps1'
    $winArgs = @{
        BuildDir = $winBuildDir
        Config = 'Release'
        RunTests = $true
    }
    if ($KillRunning) {
        $winArgs.KillRunning = $true
    }
    Write-Host '构建 Windows Release 并运行测试……' -ForegroundColor Cyan
    & $winBuildScript @winArgs

    $winOut = Join-Path $projectRoot $winBuildDir
    $exe = Join-Path $winOut 'bin\AxtQuiz.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Windows Release 产物不存在：$exe"
    }

    $stageRoot = Join-Path $projectRoot ("build\release\windows-" + $gradleVersion)
    Assert-UnderProject $stageRoot
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    $packageRoot = Join-Path $stageRoot $prefix
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

    Copy-RequiredFile -Source $exe -Destination (Join-Path $packageRoot 'AxtQuiz.exe')
    Copy-TreeFiltered -Source (Join-Path $projectRoot 'assets') `
        -Destination (Join-Path $packageRoot 'assets') -ExcludeNames @(
            '*.bak*', '*.quiz', 'chevron-left.svg', 'move-right.svg',
            'play-track-next.svg', 'redo.svg', 'software-download.svg',
            'software-upload.svg', 'CN_title.svg'
        ) -ExcludeRelativePatterns @('fonts/LxgwMarkerGothic-v1.004/*')
    Copy-TreeFiltered -Source (Join-Path $projectRoot 'LICENSES') `
        -Destination (Join-Path $packageRoot 'LICENSES')

    foreach ($name in @('LICENSE', 'NOTICE.md', 'PRIVACY.md', 'README.md',
                        'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md', 'BETA_TESTING.md',
                        '用户手册.txt')) {
        Copy-RequiredFile -Source (Join-Path $projectRoot $name) `
            -Destination (Join-Path $packageRoot $name)
    }
    foreach ($name in @('题库格式.md', '主题配色格式.md', '命名规范.md',
                        '用户界面文本草案.md')) {
        Copy-RequiredFile -Source (Join-Path $projectRoot ('docs\' + $name)) `
            -Destination (Join-Path $packageRoot ('docs\' + $name))
    }
    # README 直接引用这些截图；发布 ZIP 里也要保留，否则解压后图片是坏链。
    Copy-TreeFiltered -Source (Join-Path $projectRoot 'docs\images') `
        -Destination (Join-Path $packageRoot 'docs\images')

    Compress-Archive -Path $packageRoot -DestinationPath $windowsZip `
        -CompressionLevel Optimal
    Write-Host "Windows 发布包：$windowsZip" -ForegroundColor Green
}

if (-not $SkipAndroid) {
    $androidBuildScript = Join-Path $PSScriptRoot 'build-android.ps1'
    Write-Host '构建 Android Release……' -ForegroundColor Cyan
    & $androidBuildScript -Release
    if (-not (Test-Path -LiteralPath $androidApk -PathType Leaf)) {
        throw "Android Release 产物不存在：$androidApk"
    }
    Write-Host "Android 发布包：$androidApk" -ForegroundColor Green
}

$artifacts = @()
if (-not $SkipWindows -and (Test-Path -LiteralPath $windowsZip)) {
    $artifacts += Get-Item -LiteralPath $windowsZip
}
if (-not $SkipAndroid -and (Test-Path -LiteralPath $androidApk)) {
    $artifacts += Get-Item -LiteralPath $androidApk
}
if ($artifacts.Count -eq 0) {
    throw '没有生成任何发布产物。'
}

$hashLines = foreach ($artifact in $artifacts) {
    $hash = (Get-FileHash -LiteralPath $artifact.FullName -Algorithm SHA256).Hash
    "$hash  $($artifact.Name)"
}
Set-Content -LiteralPath $sumsPath -Value $hashLines -Encoding ASCII

$notes = @(
    "# AxtUebung $gradleVersion",
    '',
    "- 日期：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "- 提交：``$commit``（$dirtyMark）",
    "- versionCode：$versionCode",
    "- Windows：$(if ($SkipWindows) { '未生成' } else { 'Release x64 ZIP' })",
    "- Android：$(if ($SkipAndroid) { '未生成' } else { 'Release APK（当前使用调试密钥签名，仅适合侧载测试）' })",
    '',
    '## 产物',
    ''
)
foreach ($artifact in $artifacts) {
    $hash = (Get-FileHash -LiteralPath $artifact.FullName -Algorithm SHA256).Hash
    $notes += "- ``$($artifact.Name)``  ``$hash``"
}
$notes += @(
    '',
    '## 说明',
    '',
    '- Windows ZIP 不包含 `data/`、`work/`、调试符号或资源备份文件。',
    '- Android 正式公开分发前应替换项目自己的签名密钥。',
    '- 二进制文件不会由 Git 推送；AtomGit 发布页上的附件需要单独上传。'
)
Set-Content -LiteralPath $notesPath -Value $notes -Encoding UTF8

Write-Host ''
Write-Host "SHA-256：$sumsPath" -ForegroundColor Green
Write-Host "发布说明：$notesPath" -ForegroundColor Green
Write-Host '发布打包完成。' -ForegroundColor Green
