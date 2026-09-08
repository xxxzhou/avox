#!/bin/bash
# FFmpeg 9.x Linux x64 白名单构建 (LGPL, 与 Windows minsize / Android 同源)
# 差异: 无 MediaFoundation 软编(h264_mf/hevc_mf, 硬编留待 vaapi), 软编仅内置 aac;
#       TLS 走系统 OpenSSL(libssl-dev), 补齐 android 缺的 https/rtmps;
#       x86asm 关(nasm 未装, 与 windows 一致, 追求极致解码性能可装 nasm 后去掉)
# 用法 (在 WSL/Ubuntu 里, 源码目录方式):
#   ./build_ffmpeg_linux.sh [源码目录] [输出目录]
# 例:
#   ./build_ffmpeg_linux.sh ~/work/github/FFmpeg
set -e
SRC_DIR=${1:-$(pwd)}
OUT=${2:-"$SRC_DIR/out-linux-x64"}
JOBS=$(nproc 2>/dev/null || echo 8)

BUILD_DIR="$SRC_DIR/build-linux-x64"
# IN_TREE=1 时直接在源码树内构建 (源码树已有 config.h 时, out-of-tree 会被 configure 拒绝)
if [ "${IN_TREE:-0}" = "1" ]; then BUILD_DIR="$SRC_DIR"; fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

"$SRC_DIR/configure" \
  --prefix="$OUT" \
  --enable-shared --disable-static --enable-pic \
  --disable-programs --disable-doc \
  --disable-avdevice --disable-avfilter --disable-swscale \
  --disable-iconv --disable-lzma --disable-bzlib --disable-sdl2 \
  --disable-x86asm \
  --enable-zlib \
  --enable-openssl \
  --disable-everything \
  --enable-protocol=file,http,https,tcp,udp,rtp,rtmp,rtmps,rtsp,tls,srtp,crypto,data,pipe \
  --enable-demuxer=mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3 \
  --enable-muxer=mp4,mov,flv,mpegts,matroska,adts \
  --enable-decoder=h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le \
  --enable-encoder=aac \
  --enable-parser=h264,hevc,aac,mp3,opus,ac3,mpegaudio \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata

echo "== make -j$JOBS (linux x64) =="
make -j"$JOBS"
make install
echo "== 完成: $OUT =="
ls -la "$OUT/lib"
