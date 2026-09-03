#!/bin/bash

WEBRTC_DIR="/Volumes/PSSD/work/webrtc"
BUILD_TYPE="$1"
BUILD_DIR="$WEBRTC_DIR/build/ios/$BUILD_TYPE"

# iOS构建需要macOS环境和Xcode
echo "iOS构建需要macOS环境和Xcode开发工具"
echo "请确保已安装Xcode和命令行工具: xcode-select --install"

if [ "$BUILD_TYPE" != "debug" ] && [ "$BUILD_TYPE" != "release" ]; then
    echo "Error: Build type must be debug or release"
    exit 1
fi

echo "Building WebRTC iOS $BUILD_TYPE static library..."
cd "$WEBRTC_DIR/src"

IS_DEBUG=false
if [ "$$BUILD_TYPE" == "debug" ]; then
    IS_DEBUG=true
fi
# iOS构建参数配置
# target_os: ios
# target_cpu: arm64 (iPhone/iPad) 或 x64 (iOS模拟器)
# ios_enable_code_signing: false (关闭代码签名，用于静态库)

GN_ARGS="target_os=\"ios\" target_cpu=\"arm64\" is_debug=$IS_DEBUG use_rtti=true use_custom_libcxx=false rtc_use_h264=false rtc_use_h265=true rtc_include_tests=false proprietary_codecs=true rtc_build_tools=false rtc_enable_protobuf=false is_component_build=false treat_warnings_as_errors=false ios_enable_code_signing=false ios_deployment_target=\"12.0\" target_environment=\"device\""

echo "Generating GN configuration..."
gn gen "$BUILD_DIR" --args="$GN_ARGS"

echo "Building static library..."
ninja -C "$BUILD_DIR" webrtc

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo "Static library: $BUILD_DIR/obj/libwebrtc.a"
    echo "Headers: $BUILD_DIR/gen"
else
    echo "Build failed"
    exit 1
fi