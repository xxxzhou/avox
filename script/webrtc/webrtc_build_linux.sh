#!/bin/bash

WEBRTC_DIR="/home/zhouxin/work/webrtc"
BUILD_TYPE="$1"
BUILD_DIR="$WEBRTC_DIR/build/linux/$BUILD_TYPE"

# Linux构建在WSL2/Ubuntu上进行, 首次需装依赖: bash src/build/install-build-deps.sh
if [ "$BUILD_TYPE" != "debug" ] && [ "$BUILD_TYPE" != "release" ]; then
    echo "Error: Build type must be debug or release"
    exit 1
fi

echo "Building WebRTC Linux $BUILD_TYPE static library..."
cd "$WEBRTC_DIR/src"

IS_DEBUG=false
if [ "$BUILD_TYPE" == "debug" ]; then
    IS_DEBUG=true
fi

# gn/ninja: 优先用webrtc目录自带的depot_tools(Linux原生), 兜底/mnt/d挂载的
if [ -x "$WEBRTC_DIR/depot_tools/gn" ]; then
    export PATH="$WEBRTC_DIR/depot_tools:$PATH"
elif [ -x "/mnt/d/Work/webrtc/depot_tools/gn" ]; then
    export PATH="/mnt/d/Work/webrtc/depot_tools:$PATH"
fi

# Linux构建参数配置
# target_cpu: x64 (与build_linux.py一致)
# rtc_use_pipewire=false: 免装libpipewire-dev, 桌面采集暂不需要
# use_custom_libcxx=false: 用系统libstdc++, 与avox链接方式一致
GN_ARGS="target_os=\"linux\" target_cpu=\"x64\" is_debug=$IS_DEBUG use_rtti=true use_custom_libcxx=false use_thin_lto=false rtc_use_h264=true rtc_use_h265=true rtc_include_tests=false rtc_build_examples=false proprietary_codecs=true rtc_build_tools=false rtc_enable_protobuf=false is_component_build=false treat_warnings_as_errors=false rtc_use_pipewire=false"

echo "Generating GN configuration..."
gn gen "$BUILD_DIR" --args="$GN_ARGS"

echo "Building static library..."
ninja -C "$BUILD_DIR" webrtc

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# 无符号轻量版: 优先源码树自带llvm-strip, 兜底GNU strip
cp "$BUILD_DIR/obj/libwebrtc.a" "$BUILD_DIR/obj/libwebrtc_nosym.a"
if [ -x "$WEBRTC_DIR/src/third_party/llvm-build/Release+Asserts/bin/llvm-strip" ]; then
    "$WEBRTC_DIR/src/third_party/llvm-build/Release+Asserts/bin/llvm-strip" --strip-debug "$BUILD_DIR/obj/libwebrtc_nosym.a"
else
    strip --strip-debug "$BUILD_DIR/obj/libwebrtc_nosym.a"
fi

echo "Build successful!"
echo "Static library(带符号): $BUILD_DIR/obj/libwebrtc.a"
echo "Static library(无符号): $BUILD_DIR/obj/libwebrtc_nosym.a"
echo "拷贝到SDK依赖目录: cp $BUILD_DIR/obj/libwebrtc_nosym.a <avc_library>/build/linux/release/"
