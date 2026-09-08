#!/bin/bash

WEBRTC_DIR="/Volumes/PSSD/work/webrtc"
BUILD_TYPE="$1"
BUILD_DIR="$WEBRTC_DIR/build/darwin/$BUILD_TYPE"

# macOS构建,本机编本机库,目录名darwin与FindWebRTC.cmake的CMAKE_SYSTEM_NAME小写一致
echo "macOS构建需要Xcode开发工具"
echo "请确保已安装Xcode和命令行工具: xcode-select --install"

if [ "$BUILD_TYPE" != "debug" ] && [ "$BUILD_TYPE" != "release" ]; then
    echo "Error: Build type must be debug or release"
    exit 1
fi

echo "Building WebRTC macOS $BUILD_TYPE static library..."
cd "$WEBRTC_DIR/src"

IS_DEBUG=false
if [ "$BUILD_TYPE" == "debug" ]; then
    IS_DEBUG=true
fi

# depot_tools的ninja只是壳,需要brew install ninja的真ninja; gn在depot_tools里
export PATH="/opt/homebrew/bin:$WEBRTC_DIR/depot_tools:$PATH"

# macOS构建参数配置
# target_cpu: arm64 (Apple Silicon); x86_64需另行出包
# mac_deployment_target默认11.0 (src/build/config/mac/mac_sdk.gni)
# use_thin_lto=false: 出非thin归档,libwebrtc.a可单独拷走
GN_ARGS="target_os=\"mac\" target_cpu=\"arm64\" is_debug=$IS_DEBUG use_rtti=true use_custom_libcxx=false use_thin_lto=false rtc_use_h264=true rtc_use_h265=true rtc_include_tests=false rtc_build_examples=false proprietary_codecs=true rtc_build_tools=false rtc_enable_protobuf=false is_component_build=false treat_warnings_as_errors=false"

echo "Generating GN configuration..."
gn gen "$BUILD_DIR" --args="$GN_ARGS"

echo "Building static library..."
ninja -C "$BUILD_DIR" webrtc

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# 无符号轻量版: strip -S剥DWARF调试信息(约占体积93%), 与symbol_level=0等效
cp "$BUILD_DIR/obj/libwebrtc.a" "$BUILD_DIR/obj/libwebrtc_nosym.a"
strip -S "$BUILD_DIR/obj/libwebrtc_nosym.a"

echo "Build successful!"
echo "Static library(带符号): $BUILD_DIR/obj/libwebrtc.a"
echo "Static library(无符号): $BUILD_DIR/obj/libwebrtc_nosym.a"
echo "拷贝到SDK依赖目录: cp $BUILD_DIR/obj/libwebrtc*.a <avc_library>/build/darwin/release/"
