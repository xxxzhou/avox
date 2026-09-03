#!/bin/bash
# Godot Android 全链: 重建 avox_godot/libavox → 部署 → 导出 APK → 注入补丁 → 安装
# cwd 无关 (按脚本位置定位仓库根); GODOT_BIN / JAVA_HOME / ANDROID_NDK 可用环境变量覆盖
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
export ANDROID_NDK="${ANDROID_NDK:-$LOCALAPPDATA/Android/Sdk/ndk/26.1.10909125}"
export JAVA_HOME="${JAVA_HOME:-D:/Program Files/Android/Android Studio/jbr}"
GODOT_BIN="${GODOT_BIN:-/d/Work/godot/godot.exe}"
# Godot 导出要求绝对路径; 转 D:/... 风格, 无 cygpath 的环境原样使用
APK_OUT="$ROOT/platform/godot/tools/avox_tools_debug.apk"
APK_OUT_WIN="$(cygpath -m "$APK_OUT" 2>/dev/null || echo "$APK_OUT")"

python build_android.py > platform/godot/build_android_godot.log 2>&1
echo "[1/5] build OK"
python platform/godot/plugin/deploy_godot_android.py > /dev/null
echo "[2/5] deploy OK"
"$GODOT_BIN" --headless --path platform/godot/tools --export-debug "Android" "$APK_OUT_WIN" > /dev/null 2>&1
echo "[3/5] export OK"
python platform/godot/plugin/patch_apk.py "$APK_OUT_WIN" | tail -1
echo "[4/5] patch OK"
adb install -r "$APK_OUT_WIN" | tail -1
echo "[5/5] install OK"
