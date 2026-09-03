import os
import shutil
import subprocess
import build_common

# 明确指定目标系统
build_common.AVOX_TARGET_SYSTEM = "linux"
# 指定架构（x64/arm64）
build_common.AVOX_TARGET_ARCH = "x64"
# 为true会从cmake重新构建
build_common.AVOX_FORCE_REBUILD = False
# "Release" or "Debug" "RelWithDebInfo" - 优先使用环境变量
build_common.AVOX_BUILD_TYPE = os.environ.get("AVOX_BUILD_TYPE", "Release")
# 给WEB使用,AI相关的功能不需要
AVOX_WEB_CMAKE_ARGS = "-DAVOX_ENABLE_SHERPA=OFF -DAVOX_ENABLE_TRANSLATION=OFF -DAVOX_ENABLE_CV=OFF -DAVOX_ENABLE_ONNX=OFF -DAVOX_ENABLE_OPENCV=OFF"
# 配置参数 ENABLE_OPENSSL需要引用openssl,可能与webrtc的重复定义，导致webrtc/zlmedikit二者中有一个调用出现crash
ZL_CMAKE_ARGS = "-DENABLE_TESTS=OFF -DENABLE_API=ON -DENABLE_WEBRTC=OFF -DENABLE_OPENSSL=OFF -DENABLE_TESTS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
# sentencepiece - 分词库，静态库需要开-fPIC
SPM_CMAKE_ARGS = "-DSPM_ENABLE_MSVC_MT_BUILD=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
# freetype - 静态库需要开-fPIC
FREETYPE_CMAKE_ARGS = "-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DFT_DISABLE_BROTLI=ON"

SHERPA_CMAKE_ARGS = "-DSHERPA_ONNX_ENABLE_C_API=ON -DBUILD_SHARED_LIBS=ON -DSHERPA_ONNX_ENABLE_TESTS=OFF -DSHERPA_ONNX_ENABLE_EXAMPLES=OFF -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=OFF -DSHERPA_ONNX_ALREADY_EXISTS_ONNXRUNTIME=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON"

if __name__ == "__main__":
    # module可以只编译一次，有改动再编译
    if not build_common.check_module("faad2", "faad"):
        build_common.build_module("faad2")
    if not build_common.check_module_zlmediakit():
        # 注意linux对大小写敏感
        build_common.build_module("ZLMediaKit", False, ZL_CMAKE_ARGS)
    if not build_common.check_module("fdk-aac", "fdk-aac"):
        build_common.build_module("fdk-aac")
    if not build_common.check_module("freetype", "freetype"):
        build_common.build_module("freetype", False, FREETYPE_CMAKE_ARGS)
    # if not build_common.check_module_sherpa():
    #     build_common.build_module("sherpa-onnx", False, SHERPA_CMAKE_ARGS)
    # if not build_common.check_module_sentencepiece():
    #     build_common.build_module("sentencepiece", False, SPM_CMAKE_ARGS)
    # AVOX_WEB_CMAKE_ARGS
    # 支持环境变量 AVOX_EXTRA_CMAKE_ARGS
    extra_args = os.environ.get("AVOX_EXTRA_CMAKE_ARGS", "")
    # Agent/Tool 仅 Windows, 其他平台关闭
    extra_args += " -DAVOX_ENABLE_AGENT=OFF"
    # 禁用 SWIG，只编译 C++ 部分
    extra_args += " -DAVOX_ENABLE_SWIG=OFF"
    build_common.build_self(extra_args)   