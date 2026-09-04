<#
.SYNOPSIS
  把 AvoxPlayer UE 插件部署到一个 Unreal Engine 工程里, 用于实测。
  插件源码复制到 <工程>/Plugins/AvoxPlayer, 并布置 ThirdParty (avox 头 + 运行时 dll)。

.PARAMETER UeProject
  UE 工程根目录 (含 .uproject) 的路径。

.PARAMETER AvoxRelease
  avox 运行时目录 (含 avox.dll/avox.lib 及依赖 dll)。
  默认 <avox仓库>/build/windows/avplay/install/AMD64/Release。

.PARAMETER Force
  已存在 Plugins/AvoxPlayer 时删除重建 (默认保留用户改动, 只覆盖插件自带文件)。

.EXAMPLE
  .\deploy_ue.ps1 -UeProject D:\Work\MyUnrealProject
#>
param(
    [Parameter(Mandatory = $true)][string]$UeProject,
    [string]$AvoxRelease = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir "..\..")

if (-not $AvoxRelease) {
    $AvoxRelease = Join-Path $RepoRoot "build\windows\avplay\install\AMD64\Release"
}

# ── 前置检查 ──
$uproject = Get-ChildItem "$UeProject\*.uproject" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $uproject) { Write-Error "在 '$UeProject' 下没找到 .uproject, 请确认是 UE 工程根目录。" }
if (-not (Test-Path "$AvoxRelease\avox.dll")) { Write-Error "找不到 $AvoxRelease\avox.dll, 先构建 avox (python build_windows.py)。" }
$HeaderDir = Join-Path $RepoRoot "src\avox"
if (-not (Test-Path "$HeaderDir\AvoxPlayer.h")) { Write-Error "找不到 $HeaderDir\AvoxPlayer.h, 请确认 avox 仓库完整。" }

# ── 复制插件源码 ──
$PluginSrc = Join-Path $ScriptDir "plugin\AvoxPlayer"
$PluginDst = Join-Path $UeProject "Plugins\AvoxPlayer"
if ((Test-Path $PluginDst) -and $Force) {
    Write-Warning "删除已存在的 $PluginDst (-Force)"
    Remove-Item $PluginDst -Recurse -Force
}
Write-Host "==> 复制插件源码 -> $PluginDst"
New-Item -ItemType Directory -Force -Path $PluginDst | Out-Null
# ThirdParty 由下面单独布置, 排除避免误删/误覆盖
robocopy "$PluginSrc" "$PluginDst" /E /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { Write-Error "robocopy 复制插件源码失败 (code=$LASTEXITCODE)" }
$global:LASTEXITCODE = 0

# ── 布置 avox 头文件 (公共抽象类 .h) ──
$IncDst = Join-Path $PluginDst "ThirdParty\avox\include\avox"
Write-Host "==> 复制 avox 头文件 -> $IncDst"
New-Item -ItemType Directory -Force -Path $IncDst | Out-Null
Copy-Item "$HeaderDir\*.h" $IncDst -Force

# ── 布置运行时库 (链接 .lib + 延迟加载 dll) ──
$LibDst = Join-Path $PluginDst "ThirdParty\avox\lib\Win64"
Write-Host "==> 复制 avox 运行时库 -> $LibDst"
New-Item -ItemType Directory -Force -Path $LibDst | Out-Null
$Dlls = @("avox.dll", "avox.lib",
          "avcodec-61.dll", "avformat-61.dll", "avutil-59.dll", "swresample-5.dll",
          "fdk-aac.dll", "faad-2.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll", "mk_api.dll")
foreach ($dll in $Dlls) {
    $src = Join-Path $AvoxRelease $dll
    if (Test-Path $src) { Copy-Item $src $LibDst -Force }
}

# ── 运行时 dll 同时拷进插件 Binaries (编辑器期 avox.dll 延迟加载从这里解析) ──
$BinDst = Join-Path $PluginDst "Binaries\Win64"
Write-Host "==> 复制运行时 dll -> $BinDst"
New-Item -ItemType Directory -Force -Path $BinDst | Out-Null
foreach ($dll in $Dlls) {
    if ($dll -eq "avox.lib") { continue }
    $src = Join-Path $AvoxRelease $dll
    if (Test-Path $src) { Copy-Item $src $BinDst -Force }
}

Write-Host ""
Write-Host "部署完成: $PluginDst" -ForegroundColor Green
Write-Host "下一步:"
Write-Host "  1. 用 UE 5.x 打开 $($uproject.FullName), 提示发现 AvoxPlayer 插件 -> 启用并重启"
Write-Host "  2. (或编辑) 在 .uproject 的 Plugins 列表加: { \"Name\": \"AvoxPlayer\", \"Enabled\": true }"
Write-Host "  3. 关卡里给任意 Actor 加 AvoxMediaPlayer 组件, 设 Url 后 Open; 视频帧输出在组件 VideoTexture"
Write-Host "  4. 显示: 材质建 TextureSampleParameter2D (如名 AvoxTex) -> MID SetTextureParameterValue; 或 UMG Image 直接绑纹理"
Write-Host "  编辑器便捷测试: Output Log 里执行  py $ScriptDir\plugin\demo\AvoxDemoSetup.py"
