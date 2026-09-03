import os
import subprocess
import shutil
import build_common

# 当前脚本用于在windows下交叉编译android
# 如果要启用android webrtc,请在liunx下先编译android webrtc,再编译本项目
# 如果使用linux编译android,在linux添加ANDROID_NDK的环境变量,其值为NDK路径

# 明确指定目标系
build_common.AVOX_TARGET_SYSTEM = "android"
# 指定架构 armeabi-v7a/arm64-v8a
build_common.AVOX_TARGET_ARCH = "arm64-v8a"
# vscode里改C++代码，在脚本里编译，需要强制重新编译才能应用改动代码
build_common.AVOX_FORCE_REBUILD = True
# "Release" or "Debug" - 优先使用环境变量
build_common.AVOX_BUILD_TYPE = os.environ.get("AVOX_BUILD_TYPE", "Release")
# 是否只构建项目，不编译
onlyMake = False
# 安卓默认编译目录不带BuildType,因为改了BuildType这个值要设true
force_build = False

# OpenSSL for Android (HTTPS/WSS/WebRTC DTLS) - 静态链接，避免额外打包 .so
OPENSSL_DIR = build_common.find_openssl("android")
if OPENSSL_DIR:
    openssl_root = OPENSSL_DIR.replace('\\', '/')
    openssl_arch_dir = os.path.join(OPENSSL_DIR, build_common.AVOX_TARGET_ARCH)
    if os.path.exists(openssl_arch_dir):
        openssl_lib = openssl_arch_dir.replace('\\', '/')
    else:
        openssl_lib = os.path.join(OPENSSL_DIR, "lib").replace('\\', '/')
    openssl_inc = os.path.join(OPENSSL_DIR, "include").replace('\\', '/')
    OPENSSL_CMAKE_ARGS = f"-DENABLE_OPENSSL=ON -DOPENSSL_ROOT_DIR={openssl_root} -DOPENSSL_INCLUDE_DIR={openssl_inc} -DOPENSSL_CRYPTO_LIBRARY={openssl_lib}/libcrypto.a -DOPENSSL_SSL_LIBRARY={openssl_lib}/libssl.a -DOPENSSL_USE_STATIC_LIBS=ON"
else:
    OPENSSL_CMAKE_ARGS = "-DENABLE_OPENSSL=OFF"

ZL_CMAKE_ARGS = f"-DENABLE_TESTS=OFF -DENABLE_API=ON -DENABLE_SERVER=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 {OPENSSL_CMAKE_ARGS}"

FDK_AAC_CMAKE_ARGS = '-DCMAKE_C_FLAGS="-D__ANDROID_NDK__=1" -DCMAKE_CXX_FLAGS="-D__ANDROID_NDK__=1"'
# 后缀不带d
FREETYPE_CMAKE_ARGS = "-DDISABLE_FORCE_DEBUG_POSTFIX=ON"
# sherpa-onnx - 流式语音识别库，静态链接
SHERPA_CMAKE_ARGS = f"-DSHERPA_ONNX_ENABLE_C_API=ON -DBUILD_SHARED_LIBS=OFF -DSHERPA_ONNX_ENABLE_TESTS=OFF -DSHERPA_ONNX_ENABLE_EXAMPLES=OFF -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_ENABLE_TTS=OFF -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF -DSHERPA_ONNX_ENABLE_JNI=OFF -DSHERPA_ONNX_ALREADY_EXISTS_ONNXRUNTIME=ON"
# sentencepiece - 分词库，静态链接
# 注意：sentencepiece 使用 SPM_ENABLE_SHARED 而不是 BUILD_SHARED_LIBS
# -llog: sentencepiece 的 android 日志(__android_log_write)需要, 否则 spm_encode 链接失败
SPM_CMAKE_ARGS = "-DSPM_ENABLE_SHARED=OFF -DSPM_BUILD_TEST=OFF -DCMAKE_EXE_LINKER_FLAGS=-llog"

if __name__ == "__main__":
    # module可以只编译一次，有改动再编译
    if force_build or not build_common.check_module("faad2","faad"):
        build_common.build_module("faad2",onlyMake)
    if force_build or not build_common.check_module_zlmediakit():
        build_common.build_module("ZLMediaKit",onlyMake,ZL_CMAKE_ARGS)
    if force_build or not build_common.check_module("fdk-aac","fdk-aac"):
        build_common.build_module("fdk-aac",onlyMake,FDK_AAC_CMAKE_ARGS)
    if force_build or not build_common.check_module("freetype","freetype"):
        build_common.build_module("freetype",False,FREETYPE_CMAKE_ARGS)
    if force_build or not build_common.check_module_sherpa():
        build_common.build_module("sherpa-onnx", onlyMake, SHERPA_CMAKE_ARGS)
    if force_build or not build_common.check_module_sentencepiece():
        build_common.build_module("sentencepiece", onlyMake, SPM_CMAKE_ARGS)  
    swig_flag = os.environ.get("AVOX_ENABLE_SWIG", "OFF")
    godot_flag = "OFF" if os.environ.get("AVOX_GODOT_ANDROID", "1") == "0" else "ON"
    extra_args = ("-DAVOX_ENABLE_AGENT=ON -DAVOX_ENABLE_CLI=OFF "
                  f"-DAVOX_ENABLE_GODOT={godot_flag} -DAVOX_ENABLE_WEBRTC=OFF "
                  f"-DAVOX_ENABLE_SWIG={swig_flag}")
    build_common.build_self(extra_args)

    # 复制 NDK 的 libc++_shared.so 到输出目录，确保打包进 APK
    ndk_root = build_common.AVOX_NDK_ROOT
    libcxx_src = os.path.join(ndk_root, "toolchains", "llvm", "prebuilt", "windows-x86_64", "sysroot", "usr", "lib", "aarch64-linux-android", "libc++_shared.so")
    install_dir = os.path.join("install", "aarch64")
    if os.path.exists(libcxx_src):
        os.makedirs(install_dir, exist_ok=True)
        shutil.copy2(libcxx_src, install_dir)
        print(f"已复制 libc++_shared.so 到 {install_dir}")
    else:
        print(f"警告: 未找到 libc++_shared.so: {libcxx_src}")