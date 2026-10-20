<#
 ============================================================================
 AtomGit 上传（提交 / 标签 / 推送）
 ============================================================================
 做的事：
   1. 检查当前分支和 origin 是否指向 AtomGit
   2. 可显式 -StageAll 暂存所有改动；默认只提交已经暂存的内容
   3. 如有改动则提交，然后推送当前分支
   4. 可选创建并推送附注标签

 用法：
   powershell -ExecutionPolicy Bypass -File tools\upload-atomgit.ps1 -DryRun
   powershell -ExecutionPolicy Bypass -File tools\upload-atomgit.ps1 -StageAll -Message "Release 0.2.3" -Tag v0.2.3 -RunTests
   powershell -ExecutionPolicy Bypass -File tools\upload-atomgit.ps1 -Message "Fix something"

 安全规则：
   * 永不 force push；
   * 不自动暂存，除非明确写 -StageAll；
   * 默认拒绝工作区仍脏时推送，提交后可用 -AllowDirty 覆盖；
   * 只上传 Git 提交与标签；APK/ZIP 附件需在 AtomGit 发布页单独上传。
 ============================================================================
#>

param(
    [string]$Message = '',
    [switch]$StageAll,
    [string]$Remote = 'origin',
    [string]$Branch = '',
    [string]$Tag = '',
    [string]$TagMessage = '',
    [switch]$AllowDirty,
    [switch]$AllowAnyRemote,
    [switch]$RunTests,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Get-Git {
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

function Get-Lines {
    param([string[]]$Arguments)
    return @(Get-Git -Arguments $Arguments | Where-Object { "$_" -ne '' })
}

$inside = @(Get-Git -Arguments @('rev-parse', '--is-inside-work-tree') -AllowFailure)
if ($inside.Count -eq 0 -or $inside[0] -ne 'true') {
    throw "不是 Git 工作区：$projectRoot"
}

$remoteUrl = (@(Get-Git -Arguments @('remote', 'get-url', $Remote)) | Select-Object -First 1)
if (-not $remoteUrl) {
    throw "找不到远程：$Remote"
}
$remoteUrl = "$remoteUrl".Trim()
if (-not $AllowAnyRemote -and $remoteUrl -notmatch 'atomgit\.com') {
    throw "远程 $Remote 不是 AtomGit：$remoteUrl（确需使用其他远程时加 -AllowAnyRemote）"
}

if ([string]::IsNullOrWhiteSpace($Branch)) {
    $branchOutput = @(Get-Git -Arguments @('symbolic-ref', '--quiet', '--short', 'HEAD') -AllowFailure)
    if ($branchOutput.Count -eq 0) {
        throw '当前处于 detached HEAD，请先用 -Branch 指定要推送的分支。'
    }
    $Branch = "$($branchOutput[0])".Trim()
}
if ([string]::IsNullOrWhiteSpace($Branch)) {
    throw '分支名为空。'
}

$staged = Get-Lines -Arguments @('diff', '--cached', '--name-only')
$unstaged = Get-Lines -Arguments @('diff', '--name-only')
$untracked = Get-Lines -Arguments @('ls-files', '--others', '--exclude-standard')
$allChanges = $staged.Count + $unstaged.Count + $untracked.Count

Write-Host "仓库        : $projectRoot"
Write-Host "远程        : $Remote ($remoteUrl)"
Write-Host "分支        : $Branch"
Write-Host "已暂存      : $($staged.Count)"
Write-Host "未暂存      : $($unstaged.Count)"
Write-Host "未跟踪      : $($untracked.Count)"
if ($Tag) {
    Write-Host "标签        : $Tag"
}

if ($DryRun) {
    Write-Host 'DryRun：不会暂存、提交、建标签或推送。' -ForegroundColor Yellow
    if ($RunTests) {
        Write-Host '  将运行 Windows 测试。'
    }
    if ($allChanges -gt 0 -and $StageAll) {
        Write-Host '  将执行：git add -A'
    }
    if ($staged.Count -gt 0 -or ($allChanges -gt 0 -and $StageAll)) {
        if ([string]::IsNullOrWhiteSpace($Message)) {
            Write-Host '  注意：实际运行需要 -Message。' -ForegroundColor Yellow
        } else {
            Write-Host "  将执行：git commit -m `"$Message`""
        }
    }
    if ($Tag) {
        Write-Host "  将确保标签存在：$Tag"
    }
    Write-Host "  将执行：git push -u $Remote $Branch"
    if ($Tag) {
        Write-Host "  将执行：git push $Remote refs/tags/$Tag"
    }
    return
}

if ($RunTests) {
    $winTests = Join-Path $PSScriptRoot 'build-windows.ps1'
    Write-Host '运行 Windows 构建与测试……' -ForegroundColor Cyan
    & $winTests -RunTests
}

if ($StageAll) {
    if ($allChanges -gt 0) {
        Get-Git -Arguments @('add', '-A') | Out-Null
        Write-Host '已暂存全部改动。' -ForegroundColor DarkGray
    }
    $staged = Get-Lines -Arguments @('diff', '--cached', '--name-only')
    $unstaged = Get-Lines -Arguments @('diff', '--name-only')
    $untracked = Get-Lines -Arguments @('ls-files', '--others', '--exclude-standard')
}

if ($staged.Count -gt 0) {
    if ([string]::IsNullOrWhiteSpace($Message)) {
        throw '有已暂存改动，但没有 -Message，未提交。'
    }
    Write-Host "提交：$Message" -ForegroundColor Cyan
    Get-Git -Arguments @('commit', '-m', $Message) | ForEach-Object { Write-Host $_ }
} elseif (($unstaged.Count + $untracked.Count) -gt 0) {
    throw '存在未暂存或未跟踪文件。请先自行暂存，或明确使用 -StageAll。'
}

$remaining = Get-Lines -Arguments @('status', '--porcelain')
if ($remaining.Count -gt 0 -and -not $AllowDirty) {
    throw ("提交后工作区仍有改动，未推送。`n{0}`n确需推送当前提交时加 -AllowDirty。" -f ($remaining -join "`n"))
}

if ($Tag) {
    Get-Git -Arguments @('check-ref-format', "refs/tags/$Tag") | Out-Null
    $existingTag = @(Get-Git -Arguments @('rev-parse', '--quiet', '--verify', "refs/tags/$Tag") -AllowFailure)
    if ($existingTag.Count -gt 0) {
        $head = (@(Get-Git -Arguments @('rev-parse', 'HEAD')) | Select-Object -First 1).Trim()
        $tagCommit = (@(Get-Git -Arguments @('rev-list', '-n', '1', $Tag)) | Select-Object -First 1).Trim()
        if ($head -ne $tagCommit) {
            throw "标签 $Tag 已存在，但不指向当前 HEAD，拒绝覆盖。"
        }
        Write-Host "标签已存在且指向当前 HEAD：$Tag" -ForegroundColor DarkGray
    } else {
        $note = if ([string]::IsNullOrWhiteSpace($TagMessage)) { $Tag } else { $TagMessage }
        Get-Git -Arguments @('tag', '-a', $Tag, '-m', $note) | ForEach-Object { Write-Host $_ }
        Write-Host "已创建标签：$Tag" -ForegroundColor DarkGray
    }
}

Write-Host "推送分支：$Remote/$Branch" -ForegroundColor Cyan
Get-Git -Arguments @('push', '-u', $Remote, $Branch) | ForEach-Object { Write-Host $_ }

if ($Tag) {
    Write-Host "推送标签：$Tag" -ForegroundColor Cyan
    Get-Git -Arguments @('push', $Remote, "refs/tags/$Tag") | ForEach-Object { Write-Host $_ }
}

$head = (@(Get-Git -Arguments @('rev-parse', '--short', 'HEAD')) | Select-Object -First 1).Trim()
Write-Host ''
Write-Host "上传完成：$Branch @ $head" -ForegroundColor Green
if ($Tag) {
    Write-Host "标签：$Tag"
}
Write-Host "AtomGit：$remoteUrl"
