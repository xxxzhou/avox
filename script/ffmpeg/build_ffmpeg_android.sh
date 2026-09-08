#!/bin/bash
# FFmpeg 9.x Android arm64-v8a 白名单交叉构建 (NDK)
# 白名单与 Windows minsize 同源; 差异: 无 MediaFoundation 软编(硬编走 avox 自己的 MediaCodec 模块),
# 无 vulkan 硬解(走 avox_vulkan), TLS 无系统后端(https/rtmps 需另接 mbedtls/openssl, 暂缺)
# 用法 (在 MSYS2 bash 或 Git Bash 里):
#   ./build_ffmpeg_android.sh <NDK路径-msys风格> [源码目录] [输出目录]
# 例:
#   ./build_ffmpeg_android.sh /c/Users/mfjt5/AppData/Local/Android/Sdk/ndk/26.1.10909125
set -e
NDK=${1:?need NDK path (msys style, e.g. /c/Users/.../ndk/26.1.10909125)}
SRC_DIR=${2:-$(pwd)}
OUT=${3:-"$SRC_DIR/out-android-arm64"}
API=24
JOBS=$(nproc 2>/dev/null || echo 8)
HOST=windows-x86_64
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST/bin"
CC="$TOOLCHAIN/aarch64-linux-android$API-clang"
CXX="$TOOLCHAIN/aarch64-linux-android$API-clang++"
SYSROOT="$NDK/toolchains/llvm/prebuilt/$HOST/sysroot"

BUILD_DIR="$SRC_DIR/build-android-arm64"
# IN_TREE=1 时直接在源码树内构建 (源码树已有 config.h 时, out-of-tree 会被 configure 拒绝)
if [ "${IN_TREE:-0}" = "1" ]; then BUILD_DIR="$SRC_DIR"; fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$SRC_DIR/configure" \
  --prefix="$OUT" \
  --enable-shared --disable-static --enable-pic \
  --enable-cross-compile --target-os=android --arch=aarch64 --cpu=armv8-a \
  --cc="$CC" --cxx="$CXX" --sysroot="$SYSROOT" \
  --ar="$TOOLCHAIN/llvm-ar" --nm="$TOOLCHAIN/llvm-nm" \
  --ranlib="$TOOLCHAIN/llvm-ranlib" --strip="$TOOLCHAIN/llvm-strip" \
  --disable-programs --disable-doc \
  --disable-avdevice --disable-avfilter --disable-swscale \
  --disable-iconv --disable-lzma --disable-bzlib --disable-sdl2 \
  --enable-zlib \
  --disable-everything \
  --enable-protocol=file,http,https,tcp,udp,rtp,rtmp,rtmps,rtsp,srtp,crypto,data,pipe \
  --enable-demuxer=mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3 \
  --enable-muxer=mp4,mov,flv,mpegts,matroska,adts \
  --enable-decoder=h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le \
  --enable-encoder=aac \
  --enable-parser=h264,hevc,aac,mp3,opus,ac3,mpegaudio \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata

echo "== make -j$JOBS (android arm64-v8a) =="
make -j"$JOBS"
make install
echo "== 完成: $OUT =="
ls -la "$OUT/lib" 2>/dev/null
