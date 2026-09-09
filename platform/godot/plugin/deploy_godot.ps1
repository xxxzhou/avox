<#
.SYNOPSIS
  把 avox_godot 插件部署到一个 Godot 项目里, 用于实测。
  bin/ 用 junction 指向 avox 构建输出 (免拷贝, 永远最新)。

.PARAMETER GodotProject
  Godot 项目根目录(含 project.godot)的路径。

.PARAMETER AvoxRelease
  avox 运行时目录 (含 avox.dll + avox_godot.dll + avox_godot.gdextension)。
  默认 ..\..\..\build\windows\avox\install\AMD64\Release (avox 根下构建输出)。

.EXAMPLE
  .\deploy_godot.ps1 -GodotProject D:\Work\MyGodotGame
#>
param(
    [Parameter(Mandatory = $true)][string]$GodotProject,
    [string]$AvoxRelease = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $AvoxRelease) {
    $AvoxRelease = Join-Path $ScriptDir "..\..\..\build\windows\avox\install\AMD64\Release"
}

if (-not (Test-Path "$GodotProject\project.godot")) {
    Write-Error "在 '$GodotProject' 下没找到 project.godot, 请确认是 Godot 项目根目录。"
}

# 插件 dll 在 avox install 树; .gdextension 是仓库内静态文件 (见 plugin/avox_godot.gdextension)。
# 不能也从 AvoxRelease 读 —— Release 里不放 .gdextension (junction 部署时避免 Godot 扫到两份)。
$PluginDll = Join-Path $AvoxRelease "avox_godot.dll"
$Gdext     = Join-Path $ScriptDir "avox_godot.gdextension"
if (-not (Test-Path $PluginDll)) { Write-Error "找不到 $PluginDll (先构建 avox, 含 AVOX_ENABLE_GODOT)" }
if (-not (Test-Path $Gdext))     { Write-Error "找不到 $Gdext (仓库内静态文件, 应随 git 存在)" }

$Addon = Join-Path $GodotProject "addons\avox_godot"
$Bin   = Join-Path $Addon "bin"
New-Item -ItemType Directory -Force -Path $Addon | Out-Null

Write-Host "==> 复制 .gdextension"
Copy-Item $Gdext $Addon -Force

Write-Host "==> bin junction -> $AvoxRelease"
$resolvedAvox = [System.IO.Path]::GetFullPath($AvoxRelease)
if (Test-Path $Bin) {
    $item = Get-Item $Bin -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        # 已是指向某处的 junction, 解析后目标不同才重建
        $target = [System.IO.Path]::GetFullPath($item.Target)
        if ($target -ne $resolvedAvox) {
            Write-Host "  (bin 指向 $target, 重建为 $resolvedAvox)"
            cmd /c rmdir "$Bin"   # 只删链接, 不碰目标目录
            New-Item -ItemType Junction -Path $Bin -Target $AvoxRelease | Out-Null
        }
    } else {
        # 旧的真实目录 (部署拷贝), 删掉换 junction
        Write-Warning "删除旧真实目录 $Bin (换成 junction)"
        Remove-Item $Bin -Recurse -Force
        New-Item -ItemType Junction -Path $Bin -Target $AvoxRelease | Out-Null
    }
} else {
    New-Item -ItemType Junction -Path $Bin -Target $AvoxRelease | Out-Null
}

Write-Host ""
Write-Host "部署完成: $Addon (bin -> junction -> $AvoxRelease)" -ForegroundColor Green
Write-Host "下一步:"
Write-Host "  1. Godot 4.3+ (Vulkan 后端) 打开 $GodotProject"
Write-Host "  2. 提示发现 avox_godot GDExtension -> 启用, 重启编辑器"
Write-Host "  3. 打开 main.tscn, 把脚本里 player.url 改成真实地址"
Write-Host "  4. F5 运行; 看 TextureRect 是否出画面 / 控制台有无报错"
Write-Host "  (示例场景见 platform/godot/tools/src; 也可看 plugin/demo)"
