<#
 ============================================================================
 Windows 端一键构建（CMake + Ninja + MSVC）
 ============================================================================
 做的事：
   1. 自动探测本机的 Visual Studio（走 vswhere），不再写死安装路径
   2. 自动探测 CMake 与 Ninja（优先用 VS 自带的，和构建环境最一致）
   3. 生成 build\win\build-windows.bat 并执行它
        - call vcvars64.bat   进入 MSVC 环境（cl.exe 才在 PATH 里）
        - cmake -G Ninja      配置
        - cmake --build       编译
   4. 产物：build\win\bin\AxtQuiz.exe（assets 会一起复制过去）

 用法：
     powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1
     powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -Config Release
     powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -RunTests
     powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 -Clean

 换电脑后为什么不会炸：
   * VS / CMake / Ninja 全部自动探测；探测不到时用 -VsRoot / -CmakeExe / -NinjaDir 手动指。
   * 如果 build 目录是别的机器留下的（缓存里记的编译器路径已经不存在），脚本会自动清掉缓存
     重新生成，避免 "CMAKE_C_COMPILER 不存在" 这类迁移常见报错。想彻底重来就加 -Clean。

 注意（踩过的坑）：
   * 批处理里那句 set "PATH=" 再赋值，是为了去掉「PATH / Path 同时存在」的重复项：
     MSBuild 遇到大小写重复的环境变量会直接报 MSB6001；Ninja 虽然不受影响，
     但顺手清理可以让脚本在更多机器上稳定工作。
   * 批处理注释只用英文：cmd 按系统 ANSI 代码页读 .bat，中文注释可能变成乱码命令。
 ============================================================================
#>

param(
    [string]$BuildDir = 'build/win',
    [ValidateSet('Debug', 'Release')] [string]$Config = 'Debug',
    [string]$VsRoot = '',
    [string]$CmakeExe = '',
    [string]$NinjaDir = '',
    [switch]$RunTests,
    [switch]$Clean,
    [switch]$KillRunning   # 构建前自动结束正在运行的 AxtQuiz.exe（默认只提示、不杀）
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

# ---- 前置检查：正在运行的 AxtQuiz.exe 会锁住输出目录里的 exe，链接会失败（LNK1104）----
$running = @(Get-Process -Name 'AxtQuiz' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    $pids = ($running | Select-Object -ExpandProperty Id) -join ', '
    Write-Host ("检测到正在运行的 AxtQuiz.exe（PID {0}）：它会锁住输出文件，链接会失败。" -f $pids) -ForegroundColor Yellow
    if ($KillRunning) {
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 600
        Write-Host '已按 -KillRunning 结束它们，继续构建。' -ForegroundColor Yellow
    } else {
        throw "构建中止：请先关闭 AxtQuiz.exe（VS 里调试的话先停止调试），或者加 -KillRunning 让脚本自动结束它。"
    }
}

# ---------------------------------------------------------------------------
# 1) 找 Visual Studio
# ---------------------------------------------------------------------------
function Find-VsRoot {
    param([string]$Hint)

    if ($Hint) {
        if (Test-Path (Join-Path $Hint 'VC\Auxiliary\Build\vcvars64.bat')) { return $Hint }
        throw "指定的 -VsRoot 下没有 VC\Auxiliary\Build\vcvars64.bat：$Hint"
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products '*' -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -property installationPath
        if (-not $found) { $found = & $vswhere -latest -products '*' -property installationPath }
        foreach ($p in @($found)) {
            if (-not $p) { continue }
            $p = $p.Trim()
            if (Test-Path (Join-Path $p 'VC\Auxiliary\Build\vcvars64.bat')) { return $p }
        }
    }

    # 退路：翻一遍常见安装目录，找带 vcvars64.bat 的那个
    $roots = @((Join-Path $env:ProgramFiles 'Microsoft Visual Studio'))
    if (${env:ProgramFiles(x86)}) {
        $roots += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    }
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        $hit = Get-ChildItem -LiteralPath $r -Directory -ErrorAction SilentlyContinue |
               ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Directory -ErrorAction SilentlyContinue } |
               Where-Object { Test-Path (Join-Path $_.FullName 'VC\Auxiliary\Build\vcvars64.bat') } |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }

    return ''
}

$vsRoot = Find-VsRoot -Hint $VsRoot
if (-not $vsRoot) {
    throw '找不到 Visual Studio（需要含 C++ 工具集的版本）。装了的话用 -VsRoot "安装目录" 指定。'
}
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'

# ---------------------------------------------------------------------------
# 2) 找 CMake 与 Ninja（优先 VS 自带的）
# ---------------------------------------------------------------------------
if (-not $CmakeExe) {
    $CmakeExe = @(
        (Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
        (Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe')
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $CmakeExe) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $CmakeExe = $cmd.Source }
}
if (-not $CmakeExe -or -not (Test-Path $CmakeExe)) {
    $CmakeExe = 'cmake'
    Write-Host '没找到 VS 自带的 CMake，改用命令行的 cmake' -ForegroundColor Yellow
}

if (-not $NinjaDir) {
    $ninjaOnPath = Get-Command ninja -ErrorAction SilentlyContinue
    $candidates = @((Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'))
    if ($ninjaOnPath) { $candidates += (Split-Path $ninjaOnPath.Source -Parent) }
    $NinjaDir = $candidates | Where-Object { $_ -and (Test-Path (Join-Path $_ 'ninja.exe')) } | Select-Object -First 1
}
if (-not $NinjaDir -or -not (Test-Path (Join-Path $NinjaDir 'ninja.exe'))) {
    throw '找不到 ninja.exe。装了的话用 -NinjaDir "Ninja 所在目录" 指定。'
}

Write-Host "项目目录      : $projectRoot"
Write-Host "Visual Studio : $vsRoot"
Write-Host "CMake         : $CmakeExe"
Write-Host "Ninja         : $(Join-Path $NinjaDir 'ninja.exe')"
Write-Host "配置          : $Config"

# ---------------------------------------------------------------------------
# 3) 构建目录 + 旧机器缓存的自动清理
# ---------------------------------------------------------------------------
$buildPath = Join-Path $projectRoot $BuildDir

if ($Clean -and (Test-Path $buildPath)) {
    Write-Host "已按 -Clean 删除构建目录：$buildPath" -ForegroundColor Yellow
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null

$cacheFile = Join-Path $buildPath 'CMakeCache.txt'
if (Test-Path $cacheFile) {
    $staleKeys = @('CMAKE_C_COMPILER:FILEPATH=', 'CMAKE_CXX_COMPILER:FILEPATH=', 'CMAKE_MAKE_PROGRAM:FILEPATH=')
    $stale = $false
    foreach ($key in $staleKeys) {
        $line = Select-String -LiteralPath $cacheFile -Pattern ('^' + [regex]::Escape($key)) -ErrorAction SilentlyContinue |
                Select-Object -First 1
        if ($line) {
            $cached = ($line.Line -split '=', 2)[1]
            if ($cached -and -not (Test-Path $cached)) { $stale = $true; break }
        }
    }

    # 工程被改名或搬家也算缓存失效（缓存里记的是旧的源码目录）
    if (-not $stale) {
        $homeLine = Select-String -LiteralPath $cacheFile -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=' -ErrorAction SilentlyContinue |
                    Select-Object -First 1
        if ($homeLine) {
            $cachedHome = ($homeLine.Line -split '=', 2)[1].Replace('/', '\').TrimEnd('\')
            $currentHome = $projectRoot.Replace('/', '\').TrimEnd('\')
            if (-not $cachedHome.Equals($currentHome, [StringComparison]::OrdinalIgnoreCase)) { $stale = $true }
        }
    }

    if ($stale) {
        Write-Host '检测到构建缓存和当前环境对不上（旧机器的编译器路径，或旧的工程目录），自动清掉重新生成……' -ForegroundColor Yellow
        foreach ($item in @('CMakeCache.txt', 'CMakeFiles', 'build.ninja', 'rules.ninja', '.ninja_deps', '.ninja_log')) {
            $p = Join-Path $buildPath $item
            if (Test-Path $p) { Remove-Item -LiteralPath $p -Recurse -Force }
        }
    }
}

# ---------------------------------------------------------------------------
# 4) 生成并执行构建批处理
#    （vcvars 必须在 cmd 里 call，PowerShell 进程里拿不到 cl.exe）
# ---------------------------------------------------------------------------
$batPath = Join-Path $buildPath 'build-windows.bat'
$batLines = @(
    '@echo off',
    'rem Generated by tools\build-windows.ps1 - you can also run it directly.',
    'setlocal',
    '',
    'rem 0) UTF-8 console. cl.exe prints the /showIncludes prefix in UTF-8, but CMake',
    'rem    decodes subprocess output with the console codepage; under the default GBK',
    'rem    codepage that prefix lands in rules.ninja as mojibake and ninja records no',
    'rem    header dependencies at all, so editing a header never rebuilds its .cpp.',
    'chcp 65001 >nul',
    '',
    'rem 1) Remove duplicated PATH/Path entries (MSBuild would fail with MSB6001).',
    'set "AXT_CLEAN_PATH=%PATH%"',
    'set "PATH="',
    'set "PATH=%AXT_CLEAN_PATH%"',
    'set "AXT_CLEAN_PATH="',
    '',
    'rem 2) Enter the MSVC environment.',
    ('call "{0}" >nul' -f $vcvars),
    'if errorlevel 1 exit /b 1',
    '',
    'rem 3) Make ninja available.',
    ('set "PATH={0};%PATH%"' -f $NinjaDir),
    '',
    'rem 4) Configure and build.',
    ('"{0}" -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE={3}' -f $CmakeExe, $projectRoot, $buildPath, $Config),
    'if errorlevel 1 exit /b 2',
    ('"{0}" --build "{1}"' -f $CmakeExe, $buildPath),
    'if errorlevel 1 exit /b 3',
    'exit /b 0'
)
Set-Content -Path $batPath -Value $batLines -Encoding ASCII
Write-Host "构建命令已写入：$batPath"

# 注意：不能只看 $LASTEXITCODE —— 生成的 .bat 里最后一句是 exit /b 0 之外还有
# cmake --build 的返回码，实践中出现过「ninja 已经 FAILED、脚本却打印构建成功」，
# 结果拿着旧 exe 继续验证（这个坑真踩过）。所以这里扫输出里的失败特征。
$buildLog = & cmd.exe /c $batPath 2>&1
$buildLog | ForEach-Object { Write-Host $_ }
$buildFailed = $LASTEXITCODE -ne 0
foreach ($line in $buildLog) {
    if ($line -match 'FAILED:|ninja: build stopped|error LNK|error C[0-9]{4}|fatal error') {
        $buildFailed = $true
    }
}
if ($buildFailed) {
    throw "构建失败（见上面的输出）"
}

# ---- 依赖扫描自检 ---------------------------------------------------------
# ninja 靠 rules.ninja 里的 msvc_deps_prefix 识别 /showIncludes 的输出行。
# 中文版 MSVC 的前缀是「注意: 包含文件:」，一旦被 CMake 记成乱码，头文件依赖就全丢了：
# 改 .h 不会重编 .cpp → 结构体布局不一致 → 运行时各种诡异崩溃（这个坑踩过三次）。
$rulesFile = Join-Path $buildPath 'CMakeFiles\rules.ninja'
$prefixOk = $false
if (Test-Path $rulesFile) {
    $prefixOk = ([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($rulesFile)) -match 'msvc_deps_prefix = (注意: 包含文件: |Note: including file: )')
}
if (-not $prefixOk) {
    Write-Host '依赖扫描前缀不正常（中文版 VS 的 /showIncludes 前缀乱码），清掉缓存重新生成……' -ForegroundColor Yellow
    foreach ($item in @('CMakeCache.txt', 'CMakeFiles', 'build.ninja', 'rules.ninja', '.ninja_deps', '.ninja_log')) {
        $gone = Join-Path $buildPath $item
        if (Test-Path $gone) { Remove-Item -LiteralPath $gone -Recurse -Force }
    }
    $retryLog = & cmd.exe /c $batPath 2>&1
    $retryLog | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0 -or ($retryLog -match 'FAILED:|ninja: build stopped')) {
        throw "重新生成后仍然构建失败"
    }
    if (([Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($rulesFile)) -match 'msvc_deps_prefix = (注意: 包含文件: |Note: including file: )')) {
        Write-Host '依赖扫描已恢复：以后改头文件会正确触发重编。' -ForegroundColor Green
    } else {
        Write-Host '警告：依赖前缀仍不正常，改头文件可能不触发重编；请删掉 build 目录后重建。' -ForegroundColor Red
    }
}
if ($LASTEXITCODE -ne 0) {
    throw "构建失败，退出码 $LASTEXITCODE（详细日志见上面的输出）"
}

$exe = Join-Path $buildPath 'bin\AxtQuiz.exe'
$runtimeAssets = Join-Path $buildPath 'bin\assets'
New-Item -ItemType Directory -Path $runtimeAssets -Force | Out-Null
Copy-Item -Path (Join-Path $projectRoot 'assets\*') -Destination $runtimeAssets -Recurse -Force
Write-Host ''
Write-Host "构建成功：$exe" -ForegroundColor Green

if ($RunTests) {
    $tests = Join-Path $buildPath 'bin\quiz_tests.exe'
    if (Test-Path $tests) {
        Write-Host '运行单元测试……' -ForegroundColor DarkGray
        & $tests
        if ($LASTEXITCODE -ne 0) {
            throw "单元测试失败，退出码 $LASTEXITCODE"
        }
    }
}

Write-Host '运行（打开窗口）：'
Write-Host "    & `"$exe`""
Write-Host '无头截图（不开窗口，直接出图片）：'
Write-Host "    & `"$exe`" --shot shot.ppm --screen home|quiz|result|settings|stats|finished|exam|examresult --size 900x1200"
Write-Host '    （点击是真注入到 ImGui 的：--click "x,y;x2,y2"；还支持 --frames N --bank N --exam N --question N --answer K --plan "3,1,0,2" --type 文本）'
Write-Host '校验题库格式 / 命令行导入导出：'
Write-Host "    & `\"$exe`\" --validate assets\banks\demo-basic.quiz"
Write-Host "    & `"$exe`" --paper any --bank 4             （打印某套卷抽出来的题与分值）"
Write-Host "    & `"$exe`" --import D:\某题库.quiz         （拷进 data\banks）"
Write-Host "    & `"$exe`" --export bank|wrong|marked    （导出到 data\export）"
Write-Host "    & `"$exe`" --pick-test open|save         （弹系统文件对话框，验证选择器用）"

