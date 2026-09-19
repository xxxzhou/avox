#!/bin/bash
# FFmpeg 9.x Linux x64 白名单构建 (LGPL, 与 Windows minsize / Android 同源)
# 差异: 无 MediaFoundation 软编(h264_mf/hevc_mf, 硬编留待 vaapi), 软编仅内置 aac;
#       TLS 走系统 OpenSSL(libssl-dev), 补齐 android 缺的 https/rtmps;
#       x86asm 关(nasm 未装, 与 windows 一致, 追求极致解码性能可装 nasm 后去掉)
# 白名单含 2026-09 NAS 老媒体扩展 + webm/无损等常用 LGPL 软解,
# 组件名已对照 FFmpeg 9.0.1 源码核实(与 build_ffmpeg.py minsize 同步维护)
# 2026-09-18 硬解补齐: --enable-vulkan + vulkan hwaccel(FFVkDecoder 备选硬解),
#       并补编此前缺失的 vaapi hwaccel(缺它时 FFVADecoder 实际逐帧静默回退软解);
#       构建依赖: libvulkan-dev(运行时 libvulkan.so.1 由驱动/系统包自带)
# 用法 (在 WSL/Ubuntu 里, 源码目录方式):
#   ./build_ffmpeg_linux.sh [源码目录] [输出目录]
# 例:
#   ./build_ffmpeg_linux.sh ~/work/github/FFmpeg
# 无 sudo 装不了 libssl-dev 时, 用 apt-get download + dpkg -x 解到用户目录, 再用
# gcc 原生搜索路径环境变量透传 (vulkan 头用本仓 khronos, FFmpeg 运行时 dlopen libvulkan):
#   CPATH=$HOME/ffdeps/usr/include:/mnt/d/Work/github/avox/3rdparty/khronos \
#   LIBRARY_PATH=$HOME/ffdeps/usr/lib/x86_64-linux-gnu \
#   ./build_ffmpeg_linux.sh ...
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
  --enable-vulkan \
  --disable-everything \
  --enable-protocol=file,http,https,tcp,udp,rtp,rtmp,rtmps,rtsp,tls,srtp,crypto,data,pipe \
  --enable-demuxer=mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3,rm,mpegps,mpegvideo,flac,ape,amr,dsf \
  --enable-muxer=mp4,mov,flv,mpegts,matroska,adts \
  --enable-decoder=h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le,mpeg1video,mpeg2video,mpeg4,h263,flv1,wmv1,wmv2,wmv3,vc1,rv10,rv20,rv30,rv40,cook,sipr,atrac3,wmav1,wmav2,wmapro,pcm_s16be,vp8,vp9,av1,theora,mjpeg,mjpegb,dvvideo,prores,msmpeg4v1,msmpeg4v2,msmpeg4v3,vorbis,flac,dca,eac3,mp2,amrnb,amrwb,adpcm_ms,adpcm_ima_wav,adpcm_g726,adpcm_g726le,alac,ape,aac_latm,pcm_dvd,pcm_bluray,dsd_lsbf,dsd_msbf,mlp,truehd \
  --enable-encoder=aac \
  --enable-hwaccel=h264_vaapi,hevc_vaapi,h264_vulkan,hevc_vulkan,vp9_vulkan,av1_vulkan \
  --enable-parser=h264,hevc,aac,mp3,opus,ac3,mpegaudio,mpeg4video,vc1,vp8,vp9,av1,vorbis,flac,dca,aac_latm,amr,mjpeg \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata \
  ${EXTRA_FLAGS:-}

echo "== make -j$JOBS (linux x64) =="
make -j"$JOBS"
make install
echo "== 完成: $OUT =="
ls -la "$OUT/lib"
