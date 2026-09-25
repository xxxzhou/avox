#!/bin/bash
# FFmpeg 9.x Android arm64-v8a 白名单交叉构建 (NDK)
# 白名单与 Windows build_ffmpeg.py minsize 同源(2026-09: NAS 老媒体扩展 + webm/无损
# 等常用 LGPL 软解, 组件名已对照 FFmpeg 9.0.1 源码核实); 差异: 无 MediaFoundation
# 软编(硬编走 avox 自己的 MediaCodec 模块), 无 vulkan 硬解(走 avox_vulkan),
# TLS 无系统后端(https/rtmps 需另接 mbedtls/openssl, 暂缺)
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
  --enable-protocol=file,http,https,tcp,udp,rtp,rtmp,rtmps,srtp,crypto,data,pipe \
  --enable-demuxer=mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3,rm,mpegps,mpegvideo,flac,ape,amr,dsf,srt,ass,webvtt,microdvd,sami,subviewer,subviewer1,realtext,pjs,mpl2,jacosub,vplayer,stl,vobsub,aiff,caf,w64,au,tta,wv,shorten,tak,mpc,mpc8,dts,eac3,xwma,ivf,swf,image2,image2pipe,rawvideo,concat \
  --enable-muxer=mp4,mov,flv,mpegts,matroska,adts \
  --enable-decoder=h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le,mpeg1video,mpeg2video,mpeg4,h263,flv,wmv1,wmv2,wmv3,vc1,rv10,rv20,rv30,rv40,cook,sipr,atrac3,wmav1,wmav2,wmapro,pcm_s16be,vp8,vp9,av1,theora,mjpeg,mjpegb,dvvideo,prores,msmpeg4v1,msmpeg4v2,msmpeg4v3,vorbis,flac,dca,eac3,mp2,amrnb,amrwb,adpcm_ms,adpcm_ima_wav,adpcm_g726,adpcm_g726le,alac,ape,aac_latm,pcm_dvd,pcm_bluray,dsd_lsbf,dsd_msbf,mlp,truehd,pgssub,movtext,ass,ssa,subrip,srt,webvtt,dvbsub,dvdsub,text,h261,h263i,h263p,vp6,vp6a,vp6f,svq1,svq3,cinepak,indeo3,indeo4,indeo5,qtrle,rpza,smc,cscd,tscc,tscc2,truemotion1,truemotion2,fraps,utvideo,lagarith,hap,magicyuv,ffv1,huffyuv,ffvhuff,msrle,msvideo1,mszh,zmbv,flashsv,flashsv2,dnxhd,cfhd,cllc,hq_hqa,hqx,cavs,avs,vvc,rawvideo,bitpacked,v210,v210x,yuv4,png,apng,gif,webp,bmp,mp1,gsm,gsm_ms,nellymoser,speex,ilbc,wavpack,tta,shorten,tak,als,mpc7,mpc8,qdm2,qdmc,on2avc,imc,mace3,mace6,twinvq,truespeech,atrac1,atrac3p,atrac9,dss_sp,wmavoice,wmalossless,xma1,xma2,evrc,qcelp,g728,g729,mp3on4,siren,comfortnoise,aptx,aptx_hd,sbc,s302m,dolby_e \
  --enable-encoder=aac \
  --enable-parser=h264,hevc,aac,opus,ac3,mpegaudio,mpegvideo,mpeg4video,vc1,vp8,vp9,av1,vorbis,flac,dca,aac_latm,amr,mjpeg \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata

echo "== make -j$JOBS (android arm64-v8a) =="
make -j"$JOBS"
make install
echo "== 完成: $OUT =="
ls -la "$OUT/lib" 2>/dev/null
