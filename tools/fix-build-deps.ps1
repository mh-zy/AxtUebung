# ============================================================================
# 修构建目录里的「依赖扫描前缀」（中文版 MSVC + Ninja 的坑）
# ----------------------------------------------------------------------------
# 现象：改了 .h 却不会重编对应的 .cpp → 结构体布局不一致 → 运行时诡异崩溃。
# 原因：cl.exe 的 /showIncludes 前缀是中文（UTF-8），CMake 按控制台代码页（GBK）
#       解码后写进 rules.ninja 成了乱码，ninja 按字节比对匹配不上，于是头文件
#       依赖一条都没记录。
# 做法：直接把 rules.ninja 里的 msvc_deps_prefix 改成正确内容，并删掉 .ninja_deps
#       让下次构建把整个工程重编一遍（重编后才能把正确的头文件依赖记录下来）。
# 用法：powershell -ExecutionPolicy Bypass -File tools\fix-build-deps.ps1
#       默认修 build\win 和 out\build\x64-Debug 两处，也可以 -BuildDir 指定。
# ============================================================================
param(
    [string[]]$BuildDir = @('build/win', 'out/build/x64-Debug')
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$correctLine = 'msvc_deps_prefix = 注意: 包含文件:  '   # 结尾是两个空格，别动

foreach ($relative in $BuildDir) {
    $buildPath = Join-Path $projectRoot $relative
    $rules = Join-Path (Join-Path $buildPath 'CMakeFiles') 'rules.ninja'
    if (-not (Test-Path -LiteralPath $rules)) {
        Write-Host ("跳过（没有 rules.ninja）：{0}" -f $relative) -ForegroundColor DarkGray
        continue
    }

    $bytes = [IO.File]::ReadAllBytes($rules)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $raw = [IO.File]::ReadAllText($rules, [Text.Encoding]::UTF8)
    $nl = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = $raw.Replace("`r`n", "`n") -split "`n"

    $changed = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].StartsWith('msvc_deps_prefix = ')) {
            if ($lines[$i] -ne $correctLine) {
                $lines[$i] = $correctLine
                $changed++
            }
        }
    }

    if ($changed -eq 0) {
        Write-Host ("{0}：前缀本来就正确，跳过。" -f $relative) -ForegroundColor Green
        continue
    }

    [IO.File]::WriteAllText($rules, ($lines -join $nl), (New-Object System.Text.UTF8Encoding($hasBom)))
    Remove-Item -LiteralPath (Join-Path $buildPath '.ninja_deps') -Force -ErrorAction SilentlyContinue
    Write-Host ("{0}：已修正 msvc_deps_prefix（{1} 处），并清掉 .ninja_deps。" -f $relative, $changed) -ForegroundColor Green
}

Write-Host ''
Write-Host '下一步：重新构建一次（下次会把工程整体重编，之后改头文件就会正确触发重编）。'
Write-Host '    powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1'
