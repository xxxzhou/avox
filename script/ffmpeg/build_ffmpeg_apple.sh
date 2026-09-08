#!/bin/bash
# FFmpeg 9.x Apple 白名单构建 (macOS arm64 静态库 / iOS arm64 静态库)
# 白名单与 avox 仓 script/ffmpeg/build_ffmpeg.py 的 minsize 同源, 差异:
#   硬编硬解走 VideoToolbox (无 h264_mf); TLS 走 SecureTransport; Apple 双平台均出 .a,
#   对齐 3rdparty/library/darwin(ffmpeg)|ios(ffmpeg) 的静态布局
# 用法:
#   ./build_ffmpeg_apple.sh macos [源码目录] [输出目录]
#   ./build_ffmpeg_apple.sh ios   [源码目录] [输出目录]
set -e
TARGET=${1:-macos}
SRC_DIR=${2:-$(pwd)}
OUT=${3:-"$SRC_DIR/out-$TARGET"}
JOBS=$(sysctl -n hw.ncpu)

COMMON_DISABLE=(--disable-programs --disable-doc --disable-avdevice
  --disable-avfilter --disable-swscale
  --disable-iconv --disable-lzma --disable-bzlib --disable-sdl2)
WHITELIST=(--disable-everything
  --enable-protocol=file,http,https,tcp,udp,rtp,rtmp,rtmps,rtsp,tls,srtp,crypto,data,pipe
  --enable-demuxer=mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3
  --enable-muxer=mp4,mov,flv,mpegts,matroska,adts
  --enable-decoder=h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le
  --enable-encoder=h264_videotoolbox,hevc_videotoolbox,aac
  --enable-parser=h264,hevc,aac,mp3,opus,ac3,mpegaudio
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata)
APPLE=(--enable-hwaccels --enable-videotoolbox --enable-zlib)

BUILD_DIR="$SRC_DIR/build-$TARGET"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CONF_COMMON=("$SRC_DIR/configure" --prefix="$OUT" "${APPLE[@]}" "${COMMON_DISABLE[@]}" "${WHITELIST[@]}")

if [ "$TARGET" = "macos" ]; then
  set -- "${CONF_COMMON[@]}" --enable-static --disable-shared --enable-pic --enable-securetransport
else
  SDK=$(xcrun -sdk iphoneos --show-sdk-path)
  set -- "${CONF_COMMON[@]}" --enable-static --disable-shared --enable-pic
  set -- "${@}" --enable-cross-compile --target-os=darwin --arch=arm64
  set -- "${@}" --cc="xcrun -sdk iphoneos clang" --sysroot="$SDK"
  set -- "${@}" --extra-cflags="-miphoneos-version-min=13.0" --extra-ldflags="-miphoneos-version-min=13.0"
fi

echo "== configure ($TARGET) =="
if ! "$@"; then
  # SecureTransport 若已被移除, 去掉 TLS 后端重试 (https/rtmps 将不可用, 其余不受影响)
  echo "== configure 失败, 去掉 --enable-securetransport 重试 =="
  ARGS=("$@"); FILTERED=()
  for a in "${ARGS[@]}"; do [ "$a" = "--enable-securetransport" ] || FILTERED+=("$a"); done
  "${FILTERED[@]}"
fi

echo "== make -j$JOBS ($TARGET) =="
make -j"$JOBS"
make install
echo "== 完成: $OUT =="
ls -la "$OUT/lib" 2>/dev/null || ls -la "$OUT/bin" 2>/dev/null
