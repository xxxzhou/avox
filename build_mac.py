import os
import subprocess
import sys
import build_common

# 当前脚本用于在 macOS 下编译 macOS 版本（arm64 为 Apple Silicon，x64 为 Intel，universal 为双架构）
# 在 macOS 上运行，需要安装 Xcode；与 iOS 共用 cmake/ios.toolchain.cmake（PLATFORM=MAC_ARM64/MAC/MAC_UNIVERSAL）

# 明确指定目标系统
build_common.AVOX_TARGET_SYSTEM = "macos"
# 指定架构（arm64 为 Apple Silicon，x64 为 Intel，universal 为通用二进制）
build_common.AVOX_TARGET_ARCH = "arm64"
# 指定构建类型（Debug 或 Release; CI 大链接内存吃紧可置 Release）
build_common.AVOX_BUILD_TYPE = os.environ.get("AVOX_BUILD_TYPE", "Debug")
# vscode里改C++代码，在脚本里编译，需要强制重新编译才能应用改动代码；CI 缓存场景可置 AVOX_FORCE_REBUILD=False 复用已编译模块
build_common.AVOX_FORCE_REBUILD = os.environ.get("AVOX_FORCE_REBUILD", "True") == "True"
# 是否只构建项目，不编译
onlyMake = False

# ZLMediaKit - 只编译 API，禁用其他功能
ZL_CMAKE_ARGS = "-DENABLE_TESTS=OFF -DENABLE_API=ON -DENABLE_SERVER=OFF -DENABLE_WEBRTC=OFF -DENABLE_OPENSSL=OFF -DENABLE_SRT=OFF -DENABLE_PLAYER=false -DENABLE_SERVER=false -DENABLE_FFMPEG=false"
# 后缀不带d
FREETYPE_CMAKE_ARGS = "-DDISABLE_FORCE_DEBUG_POSTFIX=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_BZIP2=ON"
# fdk-aac - 静态链接
FDK_AAC_CMAKE_ARGS = "-DBUILD_SHARED_LIBS=OFF -DCMAKE_DEBUG_POSTFIX="

# sherpa-onnx - 流式语音识别库，静态链接
SHERPA_CMAKE_ARGS = "-DSHERPA_ONNX_ENABLE_C_API=ON -DBUILD_SHARED_LIBS=OFF -DSHERPA_ONNX_ENABLE_TESTS=OFF -DSHERPA_ONNX_ENABLE_EXAMPLES=OFF -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_BINARY=OFF"
# sentencepiece - 分词库，静态链接
SPM_CMAKE_ARGS = "-DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_MACOSX_BUNDLE=OFF"
# 环境变量 AVOX_CMAKE_ARGS 透传额外 cmake 参数 (同 build_windows.py)
AVOX_CMAKE_ARGS = os.environ.get("AVOX_CMAKE_ARGS", "")
# 发行渠道: commercial(默认,可商用LGPL渠道,FF软编兜底按平台注入) / agpl(GPL软编libx264/libx265可用)
# 命令行 --flavor= 或环境变量 AVOX_DIST_FLAVOR 指定
DIST_FLAVOR = os.environ.get("AVOX_DIST_FLAVOR", "commercial")
for _arg in sys.argv[1:]:
    if _arg.startswith("--flavor="):
        DIST_FLAVOR = _arg.split("=", 1)[1]
if DIST_FLAVOR not in ("agpl", "commercial"):
    DIST_FLAVOR = "commercial"

if __name__ == "__main__":
    print(f"dist flavor: {DIST_FLAVOR}")
    # module可以只编译一次，有改动再编译
    if not build_common.check_module_zlmediakit():
        build_common.build_module("zlmediakit",onlyMake,ZL_CMAKE_ARGS)
    if not build_common.check_module("fdk-aac","fdk-aac"):
        build_common.build_module("fdk-aac",onlyMake,FDK_AAC_CMAKE_ARGS)
    if not build_common.check_module("freetype","freetype"):
        build_common.build_module("freetype",onlyMake,FREETYPE_CMAKE_ARGS)
    if not build_common.check_module_sherpa():
        build_common.build_module("sherpa-onnx", onlyMake, SHERPA_CMAKE_ARGS)
    if not build_common.check_module_sentencepiece():
        build_common.build_module("sentencepiece", onlyMake, SPM_CMAKE_ARGS)
    # Agent/Tool 仅 Windows, 其他平台关闭; AVOX_CMAKE_ARGS 可透传额外参数
    extra_args = "-DAVOX_ENABLE_AGENT=OFF -DAVOX_ENABLE_CLI=OFF -DAVOX_ENABLE_SWIG=OFF"
    if AVOX_CMAKE_ARGS:
        extra_args = f"{extra_args} {AVOX_CMAKE_ARGS}"
    extra_args = f"{extra_args} -DAVOX_DIST_FLAVOR={DIST_FLAVOR}"
    build_common.build_self(extra_args)
