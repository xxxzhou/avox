#!/usr/bin/env python3
"""
从 OpenVINO python wheel 提取 C++ runtime (最小 GPU 集) 到 avc_library。

OpenVINO 的 ORT Execution Provider 没有官方预编译包(NuGet 404, GitHub release 无),
要用 OpenVINO 只能走原生 C++ runtime。pip install openvino 的 wheel 内含完整 C++
运行时(include/ + libs/), 本脚本提取最小 GPU 集到 avox 约定结构, 裁掉 NPU/其他前端。

用法:
  # 先在某 venv 装 openvino: pip install openvino
  python script/openvino/extract_openvino.py
  python script/openvino/extract_openvino.py --src D:/tmp/qenv/Lib/site-packages/openvino

输出:
  ${AVOX_EXTERNAL_LIBRARY_DIR}/3rdparty/library/windows/openvino/openvino-win-x64-2026.2/
    include/   (openvino/ + oneapi/ + tbb/)
    lib/       (openvino.lib + 一组 dll + cache.json)
"""
import argparse
import os
import shutil
import sys

VERSION = "2026.2"

# 运行时必需的 dll (openvino.dll 同目录, 运行期 LoadLibrary 加载)
KEEP_DLLS = [
    "openvino.dll",                       # 主运行时
    "openvino_onnx_frontend.dll",         # 读 .onnx (Real-ESRGAN 用)
    "openvino_ir_frontend.dll",           # IR 兜底 (小, 带上)
    "openvino_intel_gpu_plugin.dll",      # Intel iGPU (UHD770)
    "openvino_intel_cpu_plugin.dll",      # CPU 回退 (Intel/AMD 通用)
    "openvino_auto_plugin.dll",           # AUTO 设备选择 (小)
    "openvino_hetero_plugin.dll",         # 异构 (小)
    "tbb12.dll",                          # TBB (openvino.dll 依赖)
    "tbbbind_2_5.dll",
    "tbbmalloc.dll",
    "tbbmalloc_proxy.dll",
]
# 链接期 import lib (只 openvino.lib 必需; onnx_frontend.lib 可选)
KEEP_LIBS = [
    "openvino.lib",
    "openvino_onnx_frontend.lib",
]
KEEP_OTHER = ["cache.json"]  # GPU 内核缓存, 加速首次推理


def find_avox_library():
    """avc_library 路径: 环境变量 > ../avc_library"""
    env = os.environ.get("AVOX_EXTERNAL_LIBRARY_DIR")
    if env and os.path.isdir(env):
        return env
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(os.path.dirname(script_dir))
    candidate = os.path.normpath(os.path.join(project_root, "..", "avc_library"))
    return candidate


def copy_tree(src, dst, label):
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    size = sum(
        os.path.getsize(os.path.join(r, f))
        for r, _, fs in os.walk(dst) for f in fs
    )
    print(f"  {label}: {dst} ({size / 1024 / 1024:.1f} MB)")


def main():
    parser = argparse.ArgumentParser(description="提取 OpenVINO C++ runtime 到 avc_library")
    parser.add_argument("--src", default=r"D:/tmp/qenv/Lib/site-packages/openvino",
                        help="pip openvino 包路径 (含 include/ libs/)")
    parser.add_argument("--version", default=VERSION)
    args = parser.parse_args()

    src = args.src
    src_include = os.path.join(src, "include")
    src_libs = os.path.join(src, "libs")
    if not os.path.isdir(src_include) or not os.path.isdir(src_libs):
        print(f"ERROR: {src} 下找不到 include/ 或 libs/ (先 pip install openvino)", file=sys.stderr)
        return 1

    avox_lib = find_avox_library()
    dst = os.path.join(avox_lib, "3rdparty", "library", "windows", "openvino",
                       f"openvino-win-x64-{args.version}")
    dst_include = os.path.join(dst, "include")
    dst_lib = os.path.join(dst, "lib")

    print(f"src: {src}")
    print(f"dst: {dst}")
    os.makedirs(dst_lib, exist_ok=True)

    # 1. include/ 全拷 (openvino/ + oneapi/ + tbb/, 头文件传递包含不能裁)
    print("\n[1/2] include/")
    copy_tree(src_include, dst_include, "include")

    # 2. lib/ 选定文件 (libs → lib 重命名)
    print("\n[2/2] lib/ (选定 dll + lib + cache.json)")
    copied, missing = [], []
    for name in KEEP_DLLS + KEEP_LIBS + KEEP_OTHER:
        s = os.path.join(src_libs, name)
        d = os.path.join(dst_lib, name)
        if os.path.isfile(s):
            shutil.copy2(s, d)
            copied.append(name)
        else:
            missing.append(name)
    lib_size = sum(os.path.getsize(os.path.join(dst_lib, f)) for f in os.listdir(dst_lib))
    print(f"  copied {len(copied)} files ({lib_size / 1024 / 1024:.1f} MB)")
    if missing:
        print(f"  WARNING missing: {missing}")

    # 汇总
    total = sum(
        os.path.getsize(os.path.join(r, f))
        for r, _, fs in os.walk(dst) for f in fs
    )
    print(f"\n完成: {dst} (总 {total / 1024 / 1024:.1f} MB)")
    print("裁掉了: NPU(~90MB) / paddle/pytorch/tensorflow/jax 前端 / C API / debug lib")
    print("\n下一步: AVOX_ENABLE_OPENVINO=ON python build_windows.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
