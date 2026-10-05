import glob
import os
import subprocess
import sys
import build_common

# 当前脚本用于在 macOS/Linux 下编译 iOS（真机 arm64 或模拟器 x86_64）
# 在 macOS 上运行，需要安装 Xcode

# 明确指定目标系统
build_common.AVOX_TARGET_SYSTEM = "ios"
# 指定架构（arm64 为真机，x86_64 为模拟器）
build_common.AVOX_TARGET_ARCH = "arm64"
# 指定构建类型（默认 Release 不带符号, 产物体积小; 需要调试时置 Debug）
build_common.AVOX_BUILD_TYPE = os.environ.get("AVOX_BUILD_TYPE", "Release")
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
    # AI 通路 (sherpa 语音识别 / onnx 推理 / translation 翻译): onnxruntime 1.23.2
    # iOS 静态库预编译入库仓 (ios/onnxruntime/onnxruntime-ios-arm64-1.23.2/, 与 mac
    # 同版本) 后默认构建; 缺库或 AVOX_SKIP_AI=1 回退跳过 (find_package 缺席自动 OFF)。
    # 模拟器无 ORT 切片校验, 沿用跳过。
    is_sim = os.environ.get("AVOX_IOS_SIM") == "1"
    ort_dir = os.path.abspath(os.path.join(os.path.dirname(__file__),
        "..", "avox_library", "3rdparty", "library", "ios", "onnxruntime",
        "onnxruntime-ios-arm64-1.23.2"))
    ai_ready = (not is_sim) and os.path.exists(os.path.join(ort_dir, "lib", "libonnxruntime.a"))
    if os.environ.get("AVOX_SKIP_AI", "0" if ai_ready else "1") == "1":
        print("AVOX_SKIP_AI=1: 跳过 sherpa-onnx / sentencepiece")
        ai_enabled = False
    elif not ai_ready:
        print("onnxruntime iOS 预编译缺失 (avox_library/.../ios/onnxruntime/), AI 回退跳过")
        ai_enabled = False
    else:
        # sherpa-onnx 链 ORT: build-ios.sh 同款 env 变量喂其 cmake
        os.environ["SHERPA_ONNXRUNTIME_INCLUDE_DIR"] = os.path.join(ort_dir, "include")
        os.environ["SHERPA_ONNXRUNTIME_LIB_DIR"] = os.path.join(ort_dir, "lib")
        if not build_common.check_module_sherpa():
            build_common.build_module("sherpa-onnx", onlyMake, SHERPA_CMAKE_ARGS)
        if not build_common.check_module_sentencepiece():
            build_common.build_module("sentencepiece", onlyMake, SPM_CMAKE_ARGS)
        # sherpa-onnx 自设归档输出 lib/Release/ (Xcode 下无 -iphoneos 后缀); 归拢到
        # check_module_sherpa / FindSherpaOnnx 的判据位 (幂等, 已在位则零拷贝)
        import shutil
        root = os.path.join(os.path.dirname(__file__), "build", "ios", "sherpa-onnx")
        dst = os.path.join(root, "Release-iphoneos")
        os.makedirs(dst, exist_ok=True)
        for pat in (os.path.join(root, "lib", "Release", "*.a"),
                    os.path.join(root, "**", "Release-iphoneos", "*.a")):
            for src in glob.glob(pat, recursive=True):
                if os.path.dirname(os.path.realpath(src)) == os.path.realpath(dst):
                    continue  # 判据目录自身, 递归 glob 扫到时跳过
                shutil.copy2(src, dst)
        ai_enabled = build_common.check_module_sherpa() and build_common.check_module_sentencepiece()
    # Agent 开(iOS 翻译腿同 mac: TLS 走 webrtc 归档里的 BoringSSL, 符号静态全在
    # libwebrtc_nosym.a(nm T _SSL_new 实证; nosym 只是防跨 .so 重导出, 静态链入无碍),
    # 头在 avox_library/src/third_party/boringssl —— 与 mac 同一条 AVOX_AGENT_USE_BORINGSSL
    # 路线, 不需要 OpenSSL); CLI/SWIG 关。缺 BoringSSL 头时 AVOXOptions 自动降级关 Agent。
    extra_args = "-DAVOX_ENABLE_AGENT=ON -DAVOX_ENABLE_CLI=OFF -DAVOX_ENABLE_SWIG=OFF"
    # AI 开时三旗标齐开 (AVOXOptions find_package 三家+全局头/宏, 缺库逐项自动 OFF);
    # 关时维持旧行为 (iOS 无 ORT 预编译时代的硬关)。vulkan 保持开: volk 动态加载只需
    # 头文件, VULKAN_SDK 未设时自动用本机 SDK 的 macOS 目录, MoltenVK 不随 INTERFACE
    # 链接(iOS 由宿主 App 自带, LinkVulkan 对 iOS 缺库已降级为警告)
    if ai_enabled:
        extra_args += " -DAVOX_ENABLE_ONNX=ON -DAVOX_ENABLE_SHERPA=ON -DAVOX_ENABLE_TRANSLATION=ON"
    else:
        extra_args += " -DAVOX_ENABLE_ONNX=OFF -DAVOX_ENABLE_SHERPA=OFF"
    # iOS 不出单测/试跑目标(含 avox_agent_tests, 与 SDK 无关)
    extra_args += " -DAVOX_BUILD_TESTS=OFF"
    if not os.environ.get("VULKAN_SDK"):
        candidates = sorted(glob.glob(os.path.expanduser("~/VulkanSDK/*/macOS")), reverse=True)
        if candidates:
            os.environ["VULKAN_SDK"] = candidates[0]
            print(f"VULKAN_SDK 未设置, 自动选用: {candidates[0]}")
    extra_args = f"{extra_args} -DAVOX_DIST_FLAVOR={DIST_FLAVOR}"
    build_common.build_self(extra_args)