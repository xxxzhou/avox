# 编译FFmpeg

> 状态: 有效 · 上次核对: 2026-09-25 · 权威源: [script/ffmpeg/build_ffmpeg.py](../../script/ffmpeg/build_ffmpeg.py)

> 本文只放**怎么重编 / 部署 / 验收**以及白名单缺口分析。
> 白名单本身的取值与每次变更的理由，以 `script/ffmpeg/build_ffmpeg.py` 文件头为唯一权威源
> (一份事实只在一个权威源，其余引用; 四平台 shell 脚本头部均声明"与 build_ffmpeg.py minsize 同源 + 本平台差异")。

---

## 0. 2026-09-25: 必须重编一次的直接原因

白名单里写的是 `--enable-decoder=...,h263,flv1,...`，但 **FFmpeg 的解码器组件名是 `flv`，`flv1` 只是 codec id**。
configure 遇到不认识的组件名**静默忽略、不报错不警告**，于是 FLV/Sorenson H.263 解码器从来没被编进去。

实测证据:

| 写法 | `CONFIG_FLV_DECODER` | Enabled decoders |
|---|---|---|
| `--enable-decoder=h263,flv1` | **0** | h263 |
| `--enable-decoder=h263,flv` | **1** | flv, h263 |

`3rdparty/library/{darwin,windows,android,ios,linux}` 五个平台的**已部署库内嵌 configure 串全是 `flv1`**
(`strings libavcodec.a | grep -oE "--enable-decoder='[^']*'"`)，所以 flv1 片子在**所有平台**都播不了，
不是某一个平台的问题。脚本已修(`flv1` → `flv`)，但**改脚本不影响已编好的库，必须重编**。

本次顺带清掉的另外两个无效名(同样是 no-op): `--enable-parser=mp3`(正确名 `mpegaudio`，已在列表)、
`--enable-protocol=rtsp`(FFmpeg 9.0 无 rtsp 协议，rtsp 是 demuxer)。

---

## 1. 四平台重编步骤

产物落位统一在 `3rdparty/library/<平台>/ffmpeg/{include,lib}`(Windows 另有 `bin/` 放运行时 dll)。
重编前先确认源码是 **FFmpeg 9.0.x**(现有部署库 avcodec 版本 62.1.101，五平台一致)。

### Windows (MSYS2 / mingw64)

前置(MSYS2):

```bash
pacman -Syu
pacman -S base-devel yasm nasm
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-pkg-config
pacman -S mingw-w64-x86_64-zlib
pacman -S mingw-w64-x86_64-vulkan-headers mingw-w64-x86_64-vulkan-loader
```

构建(**在 FFmpeg 源码目录里跑**):

```bash
python <avox>/script/ffmpeg/build_ffmpeg.py --flavor minsize            # 商业渠道推荐
python <avox>/script/ffmpeg/build_ffmpeg.py --flavor minsize-gpl        # AGPL 渠道(+libx264/265)
python <avox>/script/ffmpeg/build_ffmpeg.py --flavor minsize --deploy   # 编完自动换库(旧库备份为 ffmpeg-bak)
python <avox>/script/ffmpeg/build_ffmpeg.py --verify <install>/bin      # 扫 GPL/nonfree 标记
```

环境变量: `MSYS2_INSTALL_DIR`(默认 `C:\msys64`)、`FFMPEG_PREFIX`(安装树)。
`--deploy` 会连带补拷 mingw 运行时依赖(zlib1.dll / libwinpthread-1.dll)。

### macOS / iOS

```bash
./script/ffmpeg/build_ffmpeg_apple.sh macos [源码目录] [输出目录]
./script/ffmpeg/build_ffmpeg_apple.sh ios   [源码目录] [输出目录]
```

出 `.a` 静态库，**脚本不含 --deploy**，需手工拷到 `3rdparty/library/darwin/ffmpeg`(macOS)
与 `3rdparty/library/ios/ffmpeg`(iOS)。两条命令共用一套白名单，只有 TLS 后端和交叉编译参数不同。

### Android (NDK, arm64-v8a)

```bash
./script/ffmpeg/build_ffmpeg_android.sh <NDK路径-msys风格> [源码目录] [输出目录]
# 例: ./build_ffmpeg_android.sh /c/Users/xxx/AppData/Local/Android/Sdk/ndk/26.1.10909125
```

出 `.so`，拷到 `3rdparty/library/android/ffmpeg/{include,lib}`。脚本内 `HOST=windows-x86_64`，
若在 Linux/macOS 上交叉编译需改这个变量。`IN_TREE=1` 可在源码树内构建(源码树已有 config.h 时用)。

### Linux (x64)

```bash
./script/ffmpeg/build_ffmpeg_linux.sh [源码目录] [输出目录]
```

前置 `libssl-dev` + `libvulkan-dev`；无 sudo 时按脚本头注释用 `apt-get download + dpkg -x` 解到用户目录，
再用 `CPATH` / `LIBRARY_PATH` 透传。出 `.so`，拷到 `3rdparty/library/linux/ffmpeg`。

---

## 2. 验收: 别只看构建成功

构建成功 ≠ 组件真的编进去了。用这三招交叉验证:

```bash
# 1) 库里到底有没有这个解码器符号
nm -g 3rdparty/library/darwin/ffmpeg/lib/libavcodec.a | grep -E "[TDS] _ff_flv_decoder"

# 2) 库内嵌的 configure 串(同时也是部署库的"来源说明", 运行时等价 avcodec_configuration())
strings 3rdparty/library/darwin/ffmpeg/lib/libavcodec.a | grep -oE "--enable-decoder='[^']*'"

# 3) configure 之后直接看开关结果(minsize 构建目录下的 config_components.h)
grep CONFIG_FLV_DECODER build-*/config_components.h
```

第 1、3 招是金标准 —— 脚本里写了什么不算数，configure 认了才算数。

---

## 3. 白名单现状 (minsize)

| 类别 | 数量 | 备注 |
|---|---|---|
| decoder | 62 | 含本次修好的 `flv` |
| demuxer | 22 | |
| muxer | 6 | |
| parser | 18 | 含 2026-09-24 补的 `mpegvideo`(MPEG-PS 花屏根因) |
| bsf | 4 | |
| protocol | 13 | android 缺 `tls`(无 TLS 后端) |

跨平台有差异的只有三类，且都是有意并写在各自脚本头的: **encoder**(win `h264_mf/hevc_mf` ·
apple `h264/hevc_videotoolbox` · android/linux 仅 `aac`)、**hwaccel**(win d3d11va+vulkan ·
apple `--enable-hwaccels`+videotoolbox · android 无 · linux vaapi+vulkan)、**protocol**。

---

## 4. 缺口: 还缺哪些常用 LGPL 解码器

以 FFmpeg 9.0 源码为基准实测: 原生解码器(排除 `lib*` 外部库与平台封装)共 **515** 个符号名，
其中 **499 个在不加 `--enable-gpl` 时就能编入**(即全部 LGPL，许可不变)，
**437 个尚未收录**。下面按优先级给出可直接粘贴的组件名，**均已在本机 configure 实测 `CONFIG_*_DECODER=1`**。

### P0 · avox 源码已映射、库里却是空的(优先补)

`src/` 里有 `AV_CODEC_ID_MOV_TEXT / ASS / SSA / SUBRIP` 的映射，但白名单只有 `pgssub` —— 这几条链路现在是哑的:

```
movtext,ass,ssa,subrip,srt,webvtt,dvbsub,dvdsub,text
```

配套 demuxer(外挂字幕文件): `srt,ass,webvtt,microdvd,sami,subviewer,subviewer1,realtext,pjs,mpl2,jacosub,vplayer,stl,xsub,vobsub`

### P1 · 常见老格式 / 摄像机 / 监控 / 国内客户素材

视频:

```
h261,h263i,h263p,vp6,vp6a,vp6f,svq1,svq3,cinepak,indeo3,indeo4,indeo5,
qtrle,rpza,smc,cscd,tscc,tscc2,truemotion1,truemotion2,fraps,utvideo,lagarith,hap,magicyuv,
ffv1,huffyuv,ffvhuff,msrle,msvideo1,mszh,zmbv,flashsv,flashsv2,
dnxhd,cfhd,cavs,avs,vvc,rawvideo,bitpacked,v210,v210x,yuv4,cllc,hq_hqa,hqx
```

> `cavs` / `avs` 是国标 AVS，`vvc` 是 H.266 —— 新编码与国内客户素材会碰到。
> `dnxhd/cfhd/cllc/hq_hqa/hqx` 是摄像机与后期素材常见编码。

音频:

```
mp1,gsm,gsm_ms,nellymoser,speex,ilbc,wavpack,tta,shorten,tak,als,
qdm2,qdmc,on2avc,dss_sp,wmavoice,atrac1,atrac3p,atrac9,imc,twinvq,truespeech,
evrc,qcelp,mpc7,mpc8,wmalossless,xma1,xma2,mp3on4,g728,g729,aptx,aptx_hd,sbc,s302m,dolby_e
```

图像/封面(缩图管线与 ID3 封面常用，目前只有 `mjpeg`):

```
png,apng,gif,webp,bmp
```

配套 demuxer(容器/裸流):

```
aiff,caf,w64,au,tta,wavpack,shorten,tak,mpc,mpc8,dts,eac3,xwma,ivf,swf,image2,image2pipe,rawvideo,concat
```

### P2 · 全量补齐(可选)

`adpcm_*` 约 30 个(ima_qt / ima_amv / ima_smjpeg / yamaha / swf / xa / ea* / ct / sbpro_2-4 / aica / thp / ima_oki / ima_dk3-4 / ima_ws / ima_apc / ima_iss / zork / mtaf / ima_dat4 / ima_alp / g722 等)
与 `pcm_*` 约 28 个(f16le / f24le / f32le-be / f64le-be / s8 / u8 / u16-32 / s32 / s64 / s24be / s24daud / lxf / vidc / sga / *_planar 等)，
外加其余少见编码。单个体积很小，全加也就几百 KB，但收益递减。

### 加不动的(别费劲)

实测有 16 个即使写进白名单也编不进(需 GPL 或平台专属后端):
`adpcm_circus, adpcm_ima_escape, adpcm_ima_hvqm2, adpcm_ima_hvqm4, adpcm_ima_magix,
adpcm_ima_pda, adpcm_n64, adpcm_psxc, ahx, codec_is, h264_mmal, h264_oh, hevc_oh,
mpeg2_mmal, mpeg4_mmal, vc1_mmal`。

---

## 5. 组件名陷阱(踩过两次，务必看)

1. **configure 认的是"符号名"，不是 `ffmpeg -decoders` 显示的名字。**
   符号名取自 `libavcodec/allcodecs.c` 里的 `ff_<name>_decoder`；显示名是 `.p.name`，两者有时会不一样:

   | `ffmpeg -decoders` 显示 | configure 要写的 |
   |---|---|
   | `mov_text` | `movtext` |
   | `g726` / `g726le` | `adpcm_g726` / `adpcm_g726le` |
   | `flv1`(codec id) | `flv` |

   各类组件的符号名来源文件: decoder/encoder → `libavcodec/allcodecs.c`、
   hwaccel → `libavcodec/hwaccels.h`、parser → `libavcodec/parsers.c`、
   bsf → `libavcodec/bitstream_filters.c`、demuxer/muxer → `libavformat/allformats.c`、
   protocol → `libavformat/protocols.c`。

2. **写错名字 configure 静默忽略**，不报错、不警告，只是产物里悄悄少一个解码器。
   已踩两次: `flv1`(应为 `flv`)、段间漏逗号熔成 `pcm_s16bevp8`(导致 vp8 与 pcm_s16be 双双丢失)。

3. 自检: configure 后查 `config_components.h` 的 `CONFIG_<NAME>_DECODER`，
   或对编出来的库 `nm -g ... | grep _ff_<name>_decoder`。**脚本里写了什么不算数。**

---

## 6. 发行合规

`--enable-nonfree` 产物任何渠道都不可分发；`--enable-gpl` 产物只能进 AGPL 渠道。
用 `script/ffmpeg/build_ffmpeg.py` 按渠道出包，交付前 `--verify` 扫一遍内嵌 configure 串。
本文第 4 节列出的补充项全部为原生 LGPL 组件，**不改变任何渠道的许可属性**。

---

## 7. 外部参考

- [在Windows上编译FFmpeg库](https://zhuanlan.zhihu.com/p/16550406805)
- [NDK编译ffmpeg包含硬件加速vulkan和mediacodec](https://blog.csdn.net/flyfish1986/article/details/131555008)
- [windows系统下编译FFMPEG并开启硬件加速（INTEL，NVIDIA，AMD）](https://blog.csdn.net/selivert/article/details/126370178)
- [ffmpeg-gpu-compile-guide](https://github.com/zshnb/ffmpeg-gpu-compile-guide)

### Windows 手工流程(脚本之外的备选)

```bash
# MSYS2 mingw64 环境，切到 FFmpeg 源码目录
./configure --prefix=../../build/windows/ffmpeg --enable-shared --disable-static \
  --enable-version3 --enable-hwaccels --enable-vulkan --enable-dxva2 --enable-d3d11va
make -j4 && make install -j4 && make clean
```

调试信息版追加 `--extra-cflags="-g -ggdb -O0" --extra-ldflags="-g -ggdb"`。
注意未指定 `--prefix` 时可能装到 `C:\msys64\usr\local\bin`，需手工拷回。

### 查看某个环境支持什么

```bash
ffmpeg -hwaccels
ffmpeg -hide_banner -decoders | findstr h264        # Windows
ffmpeg -hide_banner -decoders | grep h264           # macOS/Linux
ffmpeg -hwaccel d3d11va -i input.mp4 -c:v h264_qsv out.mp4
```

### android 交叉编译补充

见 [交叉编译](../platforms/android/交叉编译.md)
