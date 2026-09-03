#!/bin/sh

# 获取项目根目录，当前脚本所在目录向上三级即为 avplay 目录
PROJECT_ROOT=$(dirname "$(dirname "$(dirname "$(pwd)")")")

# directories
SCRATCH=$(pwd)/"scratch"
ARCHS="arm64"

# 设置 FFmpeg 版本为 7.0
FFMPEG_VERSION="7.0"
export FFMPEG_VERSION
HEADER_SUFFIX=".h"
CURRENT_FOLDER=$(pwd)
FRAMEWORK_NAME="FFmpeg"
FRAMEWORK_EXT=".framework"
FRAMEWORK="$FRAMEWORK_NAME$FRAMEWORK_EXT"

# 定义 FFmpeg 源码相对路径
FFMPEG_SOURCE_PATH="$PROJECT_ROOT/3rdparty/FFmpeg"
# 定义编译输出的 include 和 lib 相对路径
BUILD_INCLUDE_PATH="$CURRENT_FOLDER/ffmpeg/include"
BUILD_LIB_PATH="$CURRENT_FOLDER/ffmpeg/lib"

BUILD_FOLDER="$CURRENT_FOLDER/FFmpeg-iOS"
BUILD_THIN_FOLDER="$CURRENT_FOLDER/thin"
BUILD_INCLUDE_FOLDER="$BUILD_FOLDER/include"
BUILD_LIB_FOLDER="$BUILD_FOLDER/lib"
OUTPUT_FOLDER="$CURRENT_FOLDER/$FRAMEWORK"
OUTPUT_INFO_PLIST_FILE="$OUTPUT_FOLDER/Info.plist"
OUTPUT_HEADER_FOLDER="$OUTPUT_FOLDER/Headers"
OUTPUT_UMBRELLA_HEADER="$OUTPUT_HEADER_FOLDER/ffmpeg.h"
OUTPUT_MODULES_FOLDER="$OUTPUT_FOLDER/Modules"
OUTPUT_MODULES_FILE="$OUTPUT_MODULES_FOLDER/module.modulemap"
VERSION_NEW_NAME="Version.h"
BUNDLE_ID="org.ffmpeg.FFmpeg"

function CreateFramework() {
  rm -rf $OUTPUT_FOLDER
  mkdir -p $OUTPUT_HEADER_FOLDER $OUTPUT_MODULES_FOLDER
}

function CompileSource() {
  for ARCH in $ARCHS; do
    mkdir -p "$SCRATCH/$ARCH"
    cd "$SCRATCH/$ARCH"

    # 配置 FFmpeg 编译选项
    $FFMPEG_SOURCE_PATH/configure \
      --prefix="$BUILD_FOLDER/$ARCH" \
      --arch=$ARCH \
      --target-os=darwin \
      --enable-cross-compile \
      --sysroot=$(xcrun --sdk iphoneos --show-sdk-path) \
      --cc=$(xcrun -sdk iphoneos -find clang) \
      --extra-cflags="-arch $ARCH -miphoneos-version-min=11.0" \
      --extra-ldflags="-arch $ARCH -miphoneos-version-min=11.0" \
      --enable-pic \
      --enable-static \
      --disable-shared \
      --disable-doc \
      --disable-programs \
      --disable-avdevice \
      --disable-swresample \
      --disable-postproc \
      --disable-avfilter \
      --disable-debug

    make -j$(sysctl -n hw.ncpu)
    make install
    cd -
  done
}

function MergeStaticLibrary() {
  mkdir -p $BUILD_LIB_PATH
  for LIB in $(find "$BUILD_FOLDER/arm64/lib" -name "*.a"); do
    LIB_NAME=$(basename $LIB)
    LIPO_INPUTS=""
    for ARCH in $ARCHS; do
      LIPO_INPUTS="$LIPO_INPUTS $BUILD_FOLDER/$ARCH/lib/$LIB_NAME"
    done
    lipo -create $LIPO_INPUTS -output "$BUILD_LIB_PATH/$LIB_NAME"
  done
}

function CopyHeaders() {
  mkdir -p $BUILD_INCLUDE_PATH
  cp -r "$BUILD_FOLDER/arm64/include/"* "$BUILD_INCLUDE_PATH/"
}

function CreateFramework() {
  rm -rf $OUTPUT_FOLDER
  mkdir -p $OUTPUT_HEADER_FOLDER $OUTPUT_MODULES_FOLDER
}

function RenameHeader() {
  local include_folder="$BUILD_INCLUDE_PATH"
  local need_replace_version_folder=""
  for folder in "$include_folder"/*; do
    local folder_name=$(basename $folder)
    local verstion_file_name="$folder_name$VERSION_NEW_NAME"
    for header in "$folder"/*; do
      local header_name=$(basename $header)

      local dst_name=$header_name
      if [ $header_name = "version.h" ]; then
        dst_name=$verstion_file_name
      fi

      local dst_folder=$OUTPUT_HEADER_FOLDER
      local file_name="$folder/$header_name"
      local dst_file_name="$dst_folder/$dst_name"
      cp $file_name $dst_file_name
      find "$dst_folder" -name "$dst_name" -type f -exec sed -i '' "s/\"version.h\"/\"$verstion_file_name\"/g" {} \;
    done
    need_replace_version_folder="$need_replace_version_folder $folder_name"
  done

  for folder_name in $need_replace_version_folder; do
    local verstion_file_name="$folder_name$VERSION_NEW_NAME"
    find $OUTPUT_HEADER_FOLDER -type f -exec sed -i '' "s/\"$folder_name\/version.h\"/\"$verstion_file_name\"/g" {} \;
  done
  find $OUTPUT_HEADER_FOLDER -type f -exec sed -i '' "s/libavformat\///g" {} \;
  find $OUTPUT_HEADER_FOLDER -type f -exec sed -i '' "s/libavutil\///g" {} \;
  find $OUTPUT_HEADER_FOLDER -type f -exec sed -i '' "s/libavcodec\///g" {} \;
}

# COPY MISSING inttypes.h
function CopyInttype() {
  local file="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/clang/include/inttypes.h"
  cp $file $OUTPUT_HEADER_FOLDER
  find $OUTPUT_HEADER_FOLDER -type f -exec sed -i '' "s/<inttypes.h>/\"inttypes.h\"/g" {} \;
}

function CreateModulemapAndUmbrellaHeader() {
  #create ffmpeg.h
  cat > $OUTPUT_UMBRELLA_HEADER <<EOF
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AudioToolbox/AudioToolbox.h>
#include "avcodec.h"
#include "avdevice.h"
#include "avfilter.h"
#include "avformat.h"
#include "avutil.h"
#include "swscale.h"
#include "swresample.h"
double FFmpegVersionNumber = $FFMPEG_VERSION;
EOF

  cat > $OUTPUT_MODULES_FILE <<EOF
framework module $FRAMEWORK_NAME {
  umbrella header "ffmpeg.h"

  export *
  module * { export * }
}
EOF
}

function CreateInfoPlist() {
  DEFAULT_iOS_SDK_VERSION=$(defaults read $(xcode-select -p)/Platforms/iPhoneOS.platform/version CFBundleShortVersionString)
  DTCompiler=$(defaults read $(xcode-select -p)/../info DTCompiler)
  DTPlatformBuild=$(defaults read $(xcode-select -p)/../info DTPlatformBuild)
  DTSDKBuild=$(defaults read $(xcode-select -p)/../info DTSDKBuild)
  DTXcode=$(defaults read $(xcode-select -p)/../info DTXcode)
  DTXcodeBuild=$(defaults read $(xcode-select -p)/../info DTXcodeBuild)
  OS_BUILD_VERSION=$(sw_vers -buildVersion)
  cat > $OUTPUT_INFO_PLIST_FILE <<EOF
  <?xml version="1.0" encoding="UTF-8"?>
  <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
  <plist version="1.0">
  <dict>
          <key>BuildMachineOSBuild</key>
          <string>$OS_BUILD_VERSION</string>
          <key>CFBundleDevelopmentRegion</key>
          <string>en</string>
          <key>CFBundleExecutable</key>
          <string>$FRAMEWORK_NAME</string>
          <key>CFBundleIdentifier</key>
          <string>$BUNDLE_ID</string>
          <key>CFBundleInfoDictionaryVersion</key>
          <string>6.0</string>
          <key>CFBundleName</key>
          <string>$FRAMEWORK_NAME</string>
          <key>CFBundlePackageType</key>
          <string>FMWK</string>
          <key>CFBundleShortVersionString</key>
          <string>$FFMPEG_VERSION</string>
          <key>CFBundleSignature</key>
          <string>????</string>
          <key>CFBundleSupportedPlatforms</key>
          <array>
          <string>iPhoneOS</string>
          </array>
          <key>CFBundleVersion</key>
          <string>1</string>
          <key>DTCompiler</key>
          <string>$DTCompiler</string>
          <key>DTPlatformBuild</key>
          <string>$DTPlatformBuild</string>
          <key>DTPlatformName</key>
          <string>iphoneos</string>
          <key>DTPlatformVersion</key>
          <string>$DEFAULT_iOS_SDK_VERSION</string>
          <key>DTSDKBuild</key>
          <string>$DTSDKBuild</string>
          <key>DTSDKName</key>
          <string>iphoneos$DEFAULT_iOS_SDK_VERSION</string>
          <key>DTXcode</key>
          <string>$DTXcode</string>
          <key>DTXcodeBuild</key>
          <string>$DTXcodeBuild</string>
          <key>MinimumOSVersion</key>
          <string>8.0</string>
          <key>UIDeviceFamily</key>
          <array>
          <integer>1</integer>
          <integer>2</integer>
          </array>
  </dict>
  </plist>
EOF
}

CompileSource
MergeStaticLibrary
CopyHeaders
CreateFramework
RenameHeader
CreateModulemapAndUmbrellaHeader
CopyInttype
CreateInfoPlist