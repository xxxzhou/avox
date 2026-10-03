#!/bin/bash
# FFmpeg 9.x Apple 白名单构建 (macOS arm64 静态库 / iOS arm64 静态库)
# 白名单与 avox 仓 script/ffmpeg/build_ffmpeg.py 的 minsize 同源(2026-09: NAS 老媒体
# 扩展 + webm/无损等常用 LGPL 软解, 组件名已对照 FFmpeg 9.0.1 源码核实), 差异:
#   硬编硬解走 VideoToolbox (无 h264_mf; --enable-hwaccels 已含 vp9/av1 的
#   videotoolbox hwaccel); TLS 走 SecureTransport; Apple 双平台均出 .a,
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
  --enable-protocol=file,http,https,tcp,udp,rtp,rtmp,rtmps,tls,srtp,crypto,data,pipe
  --enable-demuxer=mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3,rm,mpegps,mpegvideo,flac,ape,amr,dsf,srt,ass,webvtt,microdvd,sami,subviewer,subviewer1,realtext,pjs,mpl2,jacosub,vplayer,stl,vobsub,sup,aiff,caf,w64,au,tta,wv,shorten,tak,mpc,mpc8,dts,eac3,xwma,ivf,swf,image2,image2pipe,rawvideo,concat
  --enable-muxer=mp4,mov,flv,mpegts,matroska,adts
  --enable-decoder=h264,hevc,dovi_rpudec,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le,mpeg1video,mpeg2video,mpeg4,h263,flv,wmv1,wmv2,wmv3,vc1,rv10,rv20,rv30,rv40,cook,sipr,atrac3,wmav1,wmav2,wmapro,pcm_s16be,vp8,vp9,av1,theora,mjpeg,mjpegb,dvvideo,prores,msmpeg4v1,msmpeg4v2,msmpeg4v3,vorbis,flac,dca,eac3,mp2,amrnb,amrwb,adpcm_ms,adpcm_ima_wav,adpcm_g726,adpcm_g726le,alac,ape,aac_latm,pcm_dvd,pcm_bluray,dsd_lsbf,dsd_msbf,mlp,truehd,pgssub,movtext,ass,ssa,subrip,srt,webvtt,dvbsub,dvdsub,text,h261,h263i,h263p,vp6,vp6a,vp6f,svq1,svq3,cinepak,indeo3,indeo4,indeo5,qtrle,rpza,smc,cscd,tscc,tscc2,truemotion1,truemotion2,fraps,utvideo,lagarith,hap,magicyuv,ffv1,huffyuv,ffvhuff,msrle,msvideo1,mszh,zmbv,flashsv,flashsv2,dnxhd,cfhd,cllc,hq_hqa,hqx,cavs,avs,vvc,rawvideo,bitpacked,v210,v210x,yuv4,png,apng,gif,webp,bmp,mp1,gsm,gsm_ms,nellymoser,speex,ilbc,wavpack,tta,shorten,tak,als,mpc7,mpc8,qdm2,qdmc,on2avc,imc,mace3,mace6,twinvq,truespeech,atrac1,atrac3p,atrac9,dss_sp,wmavoice,wmalossless,xma1,xma2,evrc,qcelp,g728,g729,mp3on4,siren,comfortnoise,aptx,aptx_hd,sbc,s302m,dolby_e
  --enable-encoder=h264_videotoolbox,hevc_videotoolbox,aac
  --enable-parser=h264,hevc,aac,opus,ac3,mpegaudio,mpegvideo,mpeg4video,vc1,vp8,vp9,av1,vorbis,flac,dca,aac_latm,amr,mjpeg
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata)
APPLE=(--enable-hwaccels --enable-videotoolbox --enable-zlib)

# libdav1d(BSD-2, 非 LGPL): AV1 软解。FFmpeg 原生 av1 解码器是 hwaccel-only
# 包装(av1dec.c: 无 hwaccel 即 AVERROR(ENOSYS)), 无 dav1d 时"软解"名解不出
# 帧 —— 缩略图/转码链(rec.hard.decode=0)每包报错刷屏(1003 定案)。检出同级
# dav1d 树($SRC_DIR/../dav1d/out-$TARGET)即自动挂上, 缺席不启用(行为同旧)。
# ⚠️ dav1d 版本须与 WebRTC 归档自带那份同源(avox_library 的 libwebrtc 静态链
# 里已含 dav1d, 最终链接由它供符号; 现为 1.5.1-5-g8d956180)。换版本前先核
# WebRTC 侧 dav1d_version(), 否则 libdav1d 包装与实现 ABI 不一致。
DAV1D_PREFIX=${DAV1D_PREFIX:-"$SRC_DIR/../dav1d/out-$TARGET"}
DAV1D_ARGS=()
if [ -f "$DAV1D_PREFIX/lib/libdav1d.a" ]; then
  export PKG_CONFIG_PATH="$DAV1D_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  DAV1D_ARGS=(--enable-libdav1d --extra-cflags="-I$DAV1D_PREFIX/include"
              --extra-ldflags="-L$DAV1D_PREFIX/lib")
  for i in "${!WHITELIST[@]}"; do
    case "${WHITELIST[$i]}" in
      --enable-decoder=*) WHITELIST[$i]="${WHITELIST[$i]},libdav1d" ;;
    esac
  done
  echo "== libdav1d: $DAV1D_PREFIX =="
else
  echo "== libdav1d 缺席($DAV1D_PREFIX): AV1 软解不可用(仅硬解可播) =="
fi

BUILD_DIR="$SRC_DIR/build-$TARGET"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CONF_COMMON=("$SRC_DIR/configure" --prefix="$OUT" "${APPLE[@]}" "${COMMON_DISABLE[@]}" "${WHITELIST[@]}" "${DAV1D_ARGS[@]}")

if [ "$TARGET" = "macos" ]; then
  set -- "${CONF_COMMON[@]}" --enable-static --disable-shared --enable-pic --enable-securetransport
elif [ "$TARGET" = "iossim" ]; then
  # iOS 模拟器 slice(Apple Silicon arm64): 产物落 out-iossim, 与
  # 3rdparty/library/ios/ffmpeg-sim 对应; 最低版本与既有 sim slice 对齐(15.0)
  SDK=$(xcrun -sdk iphonesimulator --show-sdk-path)
  set -- "${CONF_COMMON[@]}" --enable-static --disable-shared --enable-pic
  set -- "${@}" --enable-cross-compile --target-os=darwin --arch=arm64
  set -- "${@}" --cc="xcrun -sdk iphonesimulator clang" --sysroot="$SDK"
  set -- "${@}" --extra-cflags="-mios-simulator-version-min=15.0" --extra-ldflags="-mios-simulator-version-min=15.0"
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
