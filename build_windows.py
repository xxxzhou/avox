import os
import shutil
import subprocess
import sys
import build_common

# 明确指定目标系统
build_common.AVOX_TARGET_SYSTEM = "windows"
# 指定架构（x64/x86）
build_common.AVOX_TARGET_ARCH = "x64"
# 为true会从cmake重新构建; 环境变量可覆盖(与android/mac/ios一致), 默认False增量构建
build_common.AVOX_FORCE_REBUILD = os.environ.get("AVOX_FORCE_REBUILD", "False") == "True"
# "Release" or "Debug" "RelWithDebInfo" - 优先使用环境变量, 默认 Release 不带符号(MSVC Release 不生成 .pdb)
build_common.AVOX_BUILD_TYPE = os.environ.get("AVOX_BUILD_TYPE", "Release")
# 默认全部启用, CMake 各 plugin 的 find_package 找不到库会自动跳过
# 环境变量 AVOX_CMAKE_ARGS 透传额外 cmake 参数 (如 -DAVOX_ENABLE_OPENVINO=ON -DAVOX_ENABLE_SWIG=OFF)
AVOX_CMAKE_ARGS = os.environ.get("AVOX_CMAKE_ARGS", "")
# 发行渠道: commercial(默认,可商用LGPL渠道,Windows软编走h264_mf) / agpl(GPL软编libx264/libx265可用)
# 命令行 --flavor= 或环境变量 AVOX_DIST_FLAVOR 指定
DIST_FLAVOR = os.environ.get("AVOX_DIST_FLAVOR", "commercial")
for _arg in sys.argv[1:]:
    if _arg.startswith("--flavor="):
        DIST_FLAVOR = _arg.split("=", 1)[1]
if DIST_FLAVOR not in ("agpl", "commercial"):
    DIST_FLAVOR = "commercial"
# 运行时库配置
if build_common.AVOX_BUILD_TYPE == "Debug":
    runtime_lib = "MultiThreadedDebug"
else:
    runtime_lib = "MultiThreaded"
# ZL现使用动态库mk_api,只包含C导出
# IOS里暂时只能用静态库,配置参数ENABLE_OPENSSL需要引用openssl,可能与webrtc的重复定义
# 导致webrtc/zlmedikit二者中有一个调用出现crash
ZL_CMAKE_ARGS = f"-DENABLE_TESTS=OFF -DENABLE_API=ON -DENABLE_WEBRTC=OFF -DENABLE_OPENSSL=ON -DCMAKE_MSVC_RUNTIME_LIBRARY={runtime_lib}"
# sentencepiece - 分词库 注意MT开了可能还是在使用MD,需要直接指定MSVC_RUNTIME_LIBRARY
SPM_CMAKE_ARGS = f"-DSPM_ENABLE_MSVC_MT_BUILD=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY={runtime_lib}"
# freetype - 需要指定CRT以避免 __imp_strncpy 链接错误
FREETYPE_CMAKE_ARGS = f"-DDISABLE_FORCE_DEBUG_POSTFIX=ON -DCMAKE_MSVC_RUNTIME_LIBRARY={runtime_lib}"
# sherpa-onnx - 流式语音识别库
# SHERPA_ONNX_ENABLE_C_API=ON 启用 C API（用于 C++ 集成）
# SHERPA_ONNX_ENABLE_BINARY=OFF 不构建示例程序
SHERPA_CMAKE_ARGS = "-DSHERPA_ONNX_ENABLE_C_API=ON -DBUILD_SHARED_LIBS=ON -DSHERPA_ONNX_ENABLE_TESTS=OFF -DSHERPA_ONNX_ENABLE_EXAMPLES=OFF -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=OFF -DSHERPA_ONNX_ALREADY_EXISTS_ONNXRUNTIME=ON"
# g2o - 图优化(虚拟制片标定: 弧形幕墙内参BA/手眼再优化, 插件avox_calib静态链入)
# 静态库+MT与插件运行时一致, 关闭apps/examples/tests/cholmod/csparse, 仅用dense/eigen求解器
eigen_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "3rdparty", "eigen"))
G2O_CMAKE_ARGS = f"-DBUILD_SHARED_LIBS=OFF -DG2O_BUILD_APPS=OFF -DG2O_BUILD_EXAMPLES=OFF -DG2O_BUILD_TESTS=OFF -DG2O_BUILD_BENCHMARKS=OFF -DG2O_USE_OPENMP=OFF -DG2O_USE_CHOLMOD=OFF -DG2O_USE_CSPARSE=OFF -DG2O_USE_OPENGL=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY={runtime_lib} -DEIGEN3_INCLUDE_DIR={eigen_dir}"

def check_module_g2o():
    # g2o 的输出目录由其 CMake 固定到源码 bin/ 下
    g2o_lib = os.path.join(os.path.dirname(__file__), "3rdparty", "g2o", "bin", "Release", "g2o_core.lib")
    return os.path.exists(g2o_lib)

if __name__ == "__main__":
    print(f"dist flavor: {DIST_FLAVOR}")
    AVOX_CMAKE_ARGS = f"{AVOX_CMAKE_ARGS} -DAVOX_DIST_FLAVOR={DIST_FLAVOR}"
    # module可以只编译一次，有改动再编译
    if not build_common.check_module_zlmediakit():
        build_common.build_module("zlmediakit",False,ZL_CMAKE_ARGS)
    if not build_common.check_module("fdk-aac","fdk-aac"):
        build_common.build_module("fdk-aac")
    if not build_common.check_module("freetype","freetype"):
        build_common.build_module("freetype",False,FREETYPE_CMAKE_ARGS)
    if not build_common.check_module_sherpa():
        build_common.build_module("sherpa-onnx", False, SHERPA_CMAKE_ARGS)
    if not build_common.check_module_sentencepiece():
        build_common.build_module("sentencepiece", False, SPM_CMAKE_ARGS)
    if not check_module_g2o():
        build_common.build_module("g2o", False, G2O_CMAKE_ARGS)
    # 全部启用, find_package 找不到库的 plugin 自动跳过, 用户看 plugins/ 文件夹有无 dll 即知功能可用否
    build_common.build_self(AVOX_CMAKE_ARGS) 