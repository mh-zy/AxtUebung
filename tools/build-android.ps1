<#
 ============================================================================
 Android 端一键构建 / 安装 / 出发布包（Gradle + AGP + NDK）
 ============================================================================
 做的事：
   1. 设置 JAVA_HOME / ANDROID_HOME（优先使用调用者环境；也可用参数显式指定 JDK 与 SDK）
   2. gradlew :app:assembleDebug（-Release 时是 assembleRelease）
   3. -Release：把 release APK 复制成 dist\AxtUebung-<versionName>.apk
      （versionName 直接从 app\build.gradle.kts 里读，不写死 —— 以前是手工复制，
        0.0.1 → 0.0.2 时就容易忘）
   4. -Install：adb install -r 到当前连着的设备 / 模拟器

 用法：
     powershell -ExecutionPolicy Bypass -File tools\build-android.ps1
     powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -Release
     powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -Release -Install
     powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -GradleUserHome <path>

 产物：
   debug：app\build\outputs\apk\debug\app-debug.apk
   release（-Release）：dist\AxtUebung-<版本>.apk（用调试密钥签名，手机可直接侧载）

 模拟器 / 真机怎么驱动（这一整套都实测过，照抄即可）：
   * 起模拟器：Start-Process "$env:ANDROID_HOME\emulator\emulator.exe" -ArgumentList
       '-avd','<AVD name>','-no-window','-no-audio','-no-boot-anim','-no-snapshot','-gpu','swiftshader_indirect' -WindowStyle Hidden
   * 装 + 起：adb install -r <apk>；adb shell am start -n com.axt.quiz/.MainActivity
   * 截图：adb shell screencap -p /sdcard/s.png 然后 adb pull /sdcard/s.png
   * 点系统界面（DocumentsUI 等）：adb shell uiautomator dump /sdcard/d.xml → 解析 bounds → adb shell input tap x y
     （自家 ImGui 界面进不了 uiautomator，只能看截图量坐标）
   * 看应用内部文件（仅 debug 包）：adb shell run-as com.axt.quiz ls -R files
   * 看日志：adb logcat -s AxtQuiz:V
   * 构建日志别接管道（Gradle 守护进程占住 stdout 会看起来像卡死）：本脚本已经帮你重定向到 build\gradle-<variant>.log
 ============================================================================
#>

param(
    [switch]$Release,                                    # 出 release（侧载）包并复制到 dist\
    [switch]$Install,                                    # 装到当前设备
    [string]$GradleUserHome = '',
    [string]$JavaHome = '',
    [string]$SdkRoot = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($JavaHome)) {
    if ($env:JAVA_HOME) {
        $JavaHome = $env:JAVA_HOME
    } else {
        $studioCandidates = @(
            (Join-Path $env:ProgramFiles 'Android\Android Studio\jbr'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Android Studio\jbr')
        )
        $JavaHome = $studioCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
}
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    if ($env:ANDROID_SDK_ROOT) {
        $SdkRoot = $env:ANDROID_SDK_ROOT
    } elseif ($env:ANDROID_HOME) {
        $SdkRoot = $env:ANDROID_HOME
    } else {
        $sdkCandidate = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
        if (Test-Path -LiteralPath $sdkCandidate) {
            $SdkRoot = $sdkCandidate
        }
    }
}

if ([string]::IsNullOrWhiteSpace($JavaHome) -or -not (Test-Path -LiteralPath $JavaHome)) {
    throw "找不到 JDK（请设置 JAVA_HOME 或用 -JavaHome 指定）"
}
if ([string]::IsNullOrWhiteSpace($SdkRoot) -or -not (Test-Path -LiteralPath $SdkRoot)) {
    throw "找不到 Android SDK（请设置 ANDROID_SDK_ROOT / ANDROID_HOME，或用 -SdkRoot 指定）"
}
$env:JAVA_HOME = $JavaHome
$env:ANDROID_SDK_ROOT = $SdkRoot
$env:ANDROID_HOME = $SdkRoot
if ($GradleUserHome -ne '') {
    $env:USERPROFILE = $GradleUserHome
    $env:GRADLE_USER_HOME = $GradleUserHome
}

Set-Location $projectRoot
$variant = if ($Release) { 'Release' } else { 'Debug' }
$task = ":app:assemble$variant"
Write-Host "执行 gradlew $task" -ForegroundColor DarkGray

# Gradle 守护进程会占住 stdout（管道里看起来像卡住），所以先重定向到文件再回显尾部
$logPath = Join-Path $projectRoot "build\gradle-$($variant.ToLower()).log"
New-Item -ItemType Directory -Force -Path (Split-Path $logPath -Parent) | Out-Null
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & "$projectRoot\gradlew.bat" $task --console=plain *> $logPath
    $code = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorAction
}
Get-Content $logPath -Tail 25
if ($code -ne 0) {
    throw "Gradle 构建失败，退出码 $code（完整日志：$logPath）"
}

$apk = Join-Path $projectRoot "app\build\outputs\apk\$($variant.ToLower())\app-$($variant.ToLower()).apk"
if (-not (Test-Path $apk)) {
    throw "找不到产物：$apk"
}

if ($Release) {
    # 版本号从 app\build.gradle.kts 里读，别写死
    $gradleFile = Join-Path $projectRoot 'app\build.gradle.kts'
    $match = Select-String -LiteralPath $gradleFile -Pattern 'versionName = "([^"]+)"' | Select-Object -First 1
    $version = if ($match) { $match.Matches[0].Groups[1].Value } else { 'unknown' }
    $distDir = Join-Path $projectRoot 'dist'
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null
    $dist = Join-Path $distDir "AxtUebung-$version.apk"
    Copy-Item -LiteralPath $apk -Destination $dist -Force
    $mb = [math]::Round(((Get-Item $dist).Length / 1MB), 1)
    Write-Host "发布包：$dist（$mb MB）" -ForegroundColor Green
} else {
    Write-Host "构建成功：$apk" -ForegroundColor Green
}

if ($Install) {
    $adb = Join-Path $SdkRoot 'platform-tools\adb.exe'
    Write-Host "安装到设备：$apk" -ForegroundColor DarkGray
    & $adb install -r $apk
    if ($LASTEXITCODE -ne 0) { throw 'adb install 失败' }
}

if (-not $Install) {
    Write-Host '装到设备 / 模拟器：'
    Write-Host '    powershell -ExecutionPolicy Bypass -File tools\build-android.ps1 -Install'
    Write-Host '看日志：'
    Write-Host "    & `"$SdkRoot\platform-tools\adb.exe`" logcat -s AxtQuiz:V"
}
