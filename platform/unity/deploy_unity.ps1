<#
.SYNOPSIS
  把 com.avox.player UPM 包部署到 Unity 工程里, 用于实测。
  包源码复制到 <工程>/Packages/com.avox.player, 并布置原生插件 dll。

.PARAMETER UnityProject
  Unity 工程根目录 (含 Assets/ 与 ProjectSettings/) 的路径。

.PARAMETER AvoxRelease
  avox 运行时目录 (含 avox.dll/avox_unity.dll 及依赖 dll)。
  默认 <avox仓库>/build/windows/avplay/install/AMD64/Release。

.PARAMETER Force
  已存在 Packages/com.avox.player 时删除重建 (默认保留用户改动, 只覆盖包自带文件)。

.EXAMPLE
  .\deploy_unity.ps1 -UnityProject D:\Work\MyUnityProject
#>
param(
    [Parameter(Mandatory = $true)][string]$UnityProject,
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
if (-not (Test-Path "$UnityProject\ProjectSettings")) {
    Write-Error "在 '$UnityProject' 下没找到 ProjectSettings, 请确认是 Unity 工程根目录。"
}
if (-not (Test-Path "$AvoxRelease\avox.dll")) {
    Write-Error "找不到 $AvoxRelease\avox.dll, 先构建 avox (python build_windows.py, 顶层 AVOX_ENABLE_UNITY 默认 ON 产出 avox_unity.dll)。"
}
$UnityDll = Join-Path $AvoxRelease "avox_unity.dll"
if (-not (Test-Path $UnityDll)) {
    Write-Error "找不到 $UnityDll, 请重新 configure + build (AVOX_ENABLE_UNITY 默认 ON)。"
}

# ── 复制 UPM 包源码 ──
$PkgSrc = Join-Path $ScriptDir "plugin\unity\com.avox.player"
$PkgDst = Join-Path $UnityProject "Packages\com.avox.player"
if ((Test-Path $PkgDst) -and $Force) {
    Write-Warning "删除已存在的 $PkgDst (-Force)"
    Remove-Item $PkgDst -Recurse -Force
}
Write-Host "==> 复制 UPM 包 -> $PkgDst"
New-Item -ItemType Directory -Force -Path $PkgDst | Out-Null
robocopy "$PkgSrc" "$PkgDst" /E /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { Write-Error "robocopy 复制包失败 (code=$LASTEXITCODE)" }
$global:LASTEXITCODE = 0

# ── 布置原生插件 (Unity 自动导入 Runtime/Plugins/Windows/x86_64 下的 dll) ──
$BinDst = Join-Path $PkgDst "Runtime\Plugins\Windows\x86_64"
Write-Host "==> 复制原生插件 -> $BinDst"
New-Item -ItemType Directory -Force -Path $BinDst | Out-Null
$Dlls = @("avox_unity.dll", "avox.dll",
          "avcodec-61.dll", "avformat-61.dll", "avutil-59.dll", "swresample-5.dll",
          "fdk-aac.dll", "faad-2.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll", "mk_api.dll")
foreach ($dll in $Dlls) {
    $src = Join-Path $AvoxRelease $dll
    if (Test-Path $src) { Copy-Item $src $BinDst -Force }
}
# avox 动态插件 (WebRTC 等): avox 运行期从 <avox.dll目录>/plugins/ 扫描加载,
# AvoxRtcPlayer 组件依赖 plugins/avox_webrtc.dll, 缺失时 avoxRtcCreate 返回 null
$RtcDll = Join-Path $AvoxRelease "plugins\avox_webrtc.dll"
if (Test-Path $RtcDll) {
    New-Item -ItemType Directory -Force -Path (Join-Path $BinDst "plugins") | Out-Null
    Copy-Item $RtcDll (Join-Path $BinDst "plugins") -Force
} else {
    Write-Warning "没有 plugins\avox_webrtc.dll (WebRTC 组件将不可用, 其余功能不受影响)"
}

# ── 布置运行时资产 (glsl 着色器等, VK 离屏管线必需; 缺失表现为静默无帧) ──
$AssetSrc = Join-Path $AvoxRelease "assets"
if (Test-Path $AssetSrc) {
    Write-Host "==> 复制运行时资产 -> $BinDst\assets"
    robocopy "$AssetSrc" (Join-Path $BinDst "assets") /E /NFL /NDL /NJH /NJS | Out-Null
    if ($LASTEXITCODE -ge 8) { Write-Error "robocopy 资产复制失败 (code=$LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
} else {
    Write-Warning "构建树没有 assets 目录 ($AssetSrc), VK 离屏管线将无法渲染 (静默无帧)"
}

Write-Host ""
Write-Host "部署完成: $PkgDst" -ForegroundColor Green
Write-Host "下一步:"
Write-Host "  1. Unity 打开 $UnityProject (2021.3+), Packages 下自动导入 com.avox.player"
Write-Host "  2. 任意 GameObject 加 'Avox/Avox Media Player' 组件, 设 Url, 勾 AutoPlay 或调 Open()"
Write-Host "  3. GPU 直通: Player Settings 切 Vulkan 图形 API (-auto 或 -force-vulkan); D3D11 自动走 CPU 回退"
Write-Host "  4. 视频纹理自动绑 Renderer (MaterialPropertyBlock _MainTex/_BaseMap), 或用 VideoTexture 属性/onTextureCreated 事件"
Write-Host "  注: avox_unity.dll 等更新后重跑本脚本即可 (Unity 重新导入)"
