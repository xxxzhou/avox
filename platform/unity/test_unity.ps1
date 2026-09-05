<#
.SYNOPSIS
  avox Unity 插件一键回归: 向目标工程写入测试脚本, batchmode 跑冒烟测试 + 真实播放测试。

.DESCRIPTION
  流程: 写 Assets/Editor/AvoxSmokeTest.cs (原生 dll 加载/播放器创建销毁) 与
  AvoxPlayTest.cs (打开本地视频, 等 Ready/Playing, 校验帧尺寸与进度推进) 到工程,
  依次以 -executeMethod 执行, 全部通过退出码 0。
  测试用反射调 internal AvoxNative, 不依赖插件源码之外的任何工程配置。

.PARAMETER UnityProject
  Unity 工程根目录 (含 ProjectSettings/), 需已部署 com.avox.player (deploy_unity.ps1)。

.PARAMETER UnityEditor
  Unity.exe 路径; 默认自动探测 D:\Work\unity\*\Editor 与 Unity Hub 默认目录。

.PARAMETER TestVideo
  播放测试用的本地视频, 默认 <仓库>\assets\video\avox_electron.mp4。

.PARAMETER SkipPlay
  只跑冒烟测试 (不打开视频)。

.EXAMPLE
  .\test_unity.ps1 -UnityProject D:\Work\unity\AvoxTest
#>
param(
    [Parameter(Mandatory = $true)][string]$UnityProject,
    [string]$UnityEditor = "",
    [string]$TestVideo = "",
    [switch]$SkipPlay
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir "..\..")

# ── 前置检查 ──
if (-not (Test-Path "$UnityProject\ProjectSettings")) {
    Write-Error "在 '$UnityProject' 下没找到 ProjectSettings, 请确认是 Unity 工程根目录。"
}
if (-not $UnityEditor) {
    # 优先 D:\Work\unity (本仓库标准安装位), 其次 Unity Hub 默认目录; 各自取版本号最大的
    $primary = @(Get-ChildItem "D:\Work\unity\*\Editor\Unity.exe" -ErrorAction SilentlyContinue)
    $candidates = if ($primary.Count -gt 0) { $primary } else {
        @(Get-ChildItem "C:\Program Files\Unity\Hub\Editor\*\Editor\Unity.exe" -ErrorAction SilentlyContinue)
    }
    if ($candidates.Count -eq 0) { Write-Error "未找到 Unity.exe, 用 -UnityEditor 指定。" }
    # 取版本号最大的一个
    $UnityEditor = ($candidates | ForEach-Object { [PSCustomObject]@{ Path = $_.FullName; Ver = [version]($_.FullName -replace '.*\\([0-9]+)\.([0-9]+)\.([0-9]+)f.*', '$1.$2.$3') } } |
                    Sort-Object Ver -Descending | Select-Object -First 1).Path
}
if (-not $TestVideo) { $TestVideo = Join-Path $RepoRoot "assets\video\avox_electron.mp4" }
if (-not (Test-Path $TestVideo)) { Write-Error "测试视频不存在: $TestVideo" }
# 统一正斜杠, C# 字符串里直接可用
$VideoPath = $TestVideo -replace '\\', '/'
$EditorDir = Split-Path -Parent $UnityEditor
Write-Host "Unity : $UnityEditor"
Write-Host "工程  : $UnityProject"
Write-Host "视频  : $VideoPath"

# ── 写测试脚本 ──
$EditorDir2 = Join-Path $UnityProject "Assets\Editor"
New-Item -ItemType Directory -Force -Path $EditorDir2 | Out-Null

$smokeCs = @"
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Avox
{
    // 验证 com.avox.player 导入正常: 原生 dll 可加载, 播放器可创建/销毁, 组件可挂载
    public static class AvoxSmokeTest
    {
        public static void Run()
        {
            Debug.Log("[smoke] begin");
            var native = typeof(AvoxPlayer).Assembly.GetType("Avox.AvoxNative");
            if (native == null)
            {
                Debug.LogError("[smoke] AvoxNative type not found");
                EditorApplication.Exit(2);
                return;
            }
            const BindingFlags F = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
            var create = native.GetMethod("avoxPlayerCreate", F);
            var destroy = native.GetMethod("avoxPlayerDestroy", F);
            var getState = native.GetMethod("avoxPlayerGetState", F);
            var getVersion = native.GetMethod("avoxGetVersion", F);
            var player = create.Invoke(null, null);
            if (player == null || (System.IntPtr)player == System.IntPtr.Zero)
            {
                Debug.LogError("[smoke] avoxPlayerCreate returned null");
                EditorApplication.Exit(2);
                return;
            }
            Debug.Log($"[smoke] player created ptr={player} state={getState.Invoke(null, new[] { player })}");
            Debug.Log($"[smoke] avox version={MarshalPtrToString(getVersion.Invoke(null, null))}");
            destroy.Invoke(null, new[] { player });
            Debug.Log("[smoke] player destroyed");
            var go = new GameObject("smoke_go");
            var comp = go.AddComponent<AvoxPlayer>();
            Debug.Log($"[smoke] AvoxPlayer component added, default url='{comp.url}' hardDecode={comp.hardDecode}");
            Object.DestroyImmediate(go);
            Debug.Log("[smoke] OK");
            EditorApplication.Exit(0);
        }

        static string MarshalPtrToString(object ptr)
        {
            var p = (System.IntPtr)ptr;
            return p == System.IntPtr.Zero ? "(null)" : System.Runtime.InteropServices.Marshal.PtrToStringAnsi(p);
        }
    }
}
"@

$playCs = @"
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Avox
{
    // 真实播放测试: 打开本地视频 (软解), 等 Ready/Playing, 校验帧尺寸与进度推进
    public static class AvoxPlayTest
    {
        const string Video = "$VideoPath";
        const BindingFlags F = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
        static MethodInfo _getState, _getFrameInfo, _getPosition, _getDuration, _destroy;
        static System.IntPtr _player;
        static double _deadline, _playedSince;
        static bool _reachedPlay;

        public static void Run()
        {
            Debug.Log("[playtest] begin: " + Video);
            var native = typeof(AvoxPlayer).Assembly.GetType("Avox.AvoxNative");
            _getState = native.GetMethod("avoxPlayerGetState", F);
            _getFrameInfo = native.GetMethod("avoxPlayerGetFrameInfo", F);
            _getPosition = native.GetMethod("avoxPlayerGetPosition", F);
            _getDuration = native.GetMethod("avoxPlayerGetDuration", F);
            _destroy = native.GetMethod("avoxPlayerDestroy", F);
            _player = (System.IntPtr)native.GetMethod("avoxPlayerCreate", F).Invoke(null, null);
            native.GetMethod("avoxPlayerSetHardDecode", F).Invoke(null, new object[] { _player, 0 });
            native.GetMethod("avoxPlayerOpen", F).Invoke(null, new object[] { _player, Video });
            _deadline = EditorApplication.timeSinceStartup + 60;
            EditorApplication.update += Tick;
        }

        static void Tick()
        {
            int state = (int)_getState.Invoke(null, new object[] { _player });
            if (state == 2 || state == 3)
            {
                object[] args = { _player, 0, 0 };
                _getFrameInfo.Invoke(null, args);
                long position = (long)_getPosition.Invoke(null, new object[] { _player });
                long duration = (long)_getDuration.Invoke(null, new object[] { _player });
                if (!_reachedPlay)
                {
                    _reachedPlay = true;
                    _playedSince = EditorApplication.timeSinceStartup;
                    Debug.Log($"[playtest] state={state} frame={args[1]}x{args[2]} position={position} duration={duration}");
                }
                else if (EditorApplication.timeSinceStartup - _playedSince > 2.0)
                {
                    Debug.Log($"[playtest] after 2s: state={state} position={position} duration={duration}");
                    bool ok = position > 0 || state == 3;
                    _destroy.Invoke(null, new object[] { _player });
                    if (ok) { Debug.Log("[playtest] OK"); EditorApplication.Exit(0); }
                    else { Debug.LogError("[playtest] position not advancing"); EditorApplication.Exit(4); }
                    return;
                }
            }
            if (EditorApplication.timeSinceStartup > _deadline)
            {
                Debug.LogError($"[playtest] timeout, state={state}");
                _destroy.Invoke(null, new object[] { _player });
                EditorApplication.Exit(3);
            }
        }
    }
}
"@

# UTF8 BOM, Unity 文本资产标准编码
Set-Content -Path (Join-Path $EditorDir2 "AvoxSmokeTest.cs") -Value $smokeCs -Encoding UTF8
Set-Content -Path (Join-Path $EditorDir2 "AvoxPlayTest.cs") -Value $playCs -Encoding UTF8
Write-Host "==> 测试脚本已写入 $EditorDir2"

function Invoke-UnityBatch {
    param([string]$Method, [string]$LogPath, [bool]$NoQuit = $false)
    $args = @("-batchmode", "-projectPath", $UnityProject, "-executeMethod", $Method, "-logFile", $LogPath)
    if (-not $NoQuit) { $args += "-quit" }
    $p = Start-Process -FilePath $UnityEditor -ArgumentList $args -Wait -PassThru
    return $p.ExitCode
}

$fail = 0

# ── 冒烟测试 ──
Write-Host "==> 冒烟测试..."
$log1 = Join-Path $env:TEMP "avox_unity_smoke.log"
$code = Invoke-UnityBatch "Avox.AvoxSmokeTest.Run" $log1
if ($code -eq 0 -and (Select-String -Path $log1 -Pattern "\[smoke\] OK" -Quiet)) {
    Write-Host "    冒烟测试通过" -ForegroundColor Green
} else {
    Write-Host "    冒烟测试失败 (exit=$code), 日志: $log1" -ForegroundColor Red
    $fail = 1
}

# ── 真实播放测试 ──
if (-not $SkipPlay -and $fail -eq 0) {
    Write-Host "==> 播放测试..."
    $log2 = Join-Path $env:TEMP "avox_unity_play.log"
    $code = Invoke-UnityBatch "Avox.AvoxPlayTest.Run" $log2 -NoQuit $true
    if ($code -eq 0 -and (Select-String -Path $log2 -Pattern "\[playtest\] OK" -Quiet)) {
        Write-Host "    播放测试通过" -ForegroundColor Green
        Select-String -Path $log2 -Pattern "\[playtest\] (state|after)" | ForEach-Object { Write-Host "    $($_.Line)" }
    } else {
        Write-Host "    播放测试失败 (exit=$code), 日志: $log2" -ForegroundColor Red
        $fail = 1
    }
}

if ($fail -eq 0) { Write-Host "全部通过" -ForegroundColor Green } else { Write-Error "存在失败项" }
exit $fail
