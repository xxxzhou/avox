import os
import subprocess
import sys

# FFmpeg 9.x 双渠道 + 裁剪构建脚本 (Windows/MSYS2)
#
# flavor:
#   gpl         AGPL 渠道, 全量内置编解码 + libx264/libx265 (无 nonfree; nonfree 产物任何渠道不可分发)
#   lgpl        商业渠道兜底, 全量内置编解码, 无 GPL 组件
#   minsize     商业渠道推荐, 白名单: 只编 avox 实际用到的协议/封装/编解码 (≈92MB -> ~15MB)
#   minsize-gpl AGPL 渠道白名单版 (minsize + libx264/libx265)
#
# 环境变量:
#   MSYS2_INSTALL_DIR  MSYS2 根目录 (默认 C:\msys64)
#   FFMPEG_PREFIX      安装树绝对路径 (默认 ../build/windows/ffmpeg-<flavor>)
#
# 用法 (在 FFmpeg 源码目录里跑):
#   python build_ffmpeg.py --flavor minsize
#   python build_ffmpeg.py --verify <dll或目录>   # 扫描 configure 串, GPL/Nonfree 标记即报错

MSYS2_INSTALL_DIR = os.environ.get("MSYS2_INSTALL_DIR", "C:\\msys64")
# subprocess 参数列表直传 (os.system 经 cmd /c 会剥离引号, 可执行路径带正斜杠时直接炸)
bash_path = os.path.join(MSYS2_INSTALL_DIR, "usr", "bin", "bash.exe")

GPL_MARKS = (
    b"--enable-gpl", b"--enable-nonfree",
    b"--enable-libx264", b"--enable-libx265",
    b"--enable-libfdk-aac",
)

# ---- 公共配置 ----
# 硬解加速保留 d3d11va/dxva2 (纯系统 API, 与 GPL 无关; minsize 白名单见 MINIMUM_HWACCELS,
# 全量 flavor 默认自带); 裁剪项均为 avox 源码零使用
# (avdevice/avfilter/swscale/postproc 无调用, exe 不随 SDK 分发)
COMMON_OPTIONS = [
    "--disable-static",
    "--enable-shared",
    "--enable-version3",
    "--disable-programs",
    "--disable-doc",
    "--disable-avdevice",
    "--disable-avfilter",
    "--disable-swscale",
    "--disable-iconv",
    "--disable-lzma",
    "--disable-bzlib",         # 9.0 选项名: bzlib (旧称 bz2 已失效)
    "--disable-sdl2",
    "--disable-x86asm",        # 免装 nasm; 追求极致解码性能可去掉
    "--enable-zlib",           # http gzip + matroska 压缩轨
    "--enable-schannel",       # https/tls/rtmps 走 Windows 自带 TLS
]
# 注: FFmpeg 9.0 已删除 postproc 库, 勿加 --disable-postproc

# ---- 白名单 (minsize): 对齐 avox 源码实际映射面 ----
MINIMUM_DECODERS = "h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le"
# hwaccel 是 avcodec 独立组件, --disable-everything 会连它一起裁掉;
# 不显式加回则 ff_get_format 拿不到 D3D11 配置, 硬解逐帧静默回退软解 (09-10 排查结论)。
# 注意 FFmpeg9 拆了新旧两个组件: d3d11va(legacy, D3D11VA_VLD, 不支持 hw_device_ctx)
# 与 d3d11va2(现代, D3D11, 走 hw_device_ctx)—— 运行时走的是 d3d11va2。
# legacy 必须同开: 9.0.1 的 Makefile 只在 legacy/dxva2 配置下才编 dxva2_h264.o
# (d3d11va2 的符号也在这个文件里), 只开 d3d11va2 会链接失败 (09-10 实测)。
# legacy 运行时无害: 无 HW_DEVICE_CTX 方法, 默认选择器会自动跳过
MINIMUM_HWACCELS = "h264_d3d11va,h264_d3d11va2,hevc_d3d11va,hevc_d3d11va2"
MINIMUM_ENCODERS = "h264_mf,hevc_mf,aac"   # 商业渠道; h264_mf/hevc_mf 为系统自带 MFT
MINIMUM_PARSERS = "h264,hevc,aac,mp3,opus,ac3,mpegaudio"
MINIMUM_BSF = "h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata"
# 直播/点播/文件: rtmp 系 + rtsp 系 + http(s) 系 + hls(crypto=AES 解密) + file
# 后续需要 SRT: 装 libsrt + --enable-libsrt --enable-protocol=srt
MINIMUM_PROTOCOLS = "file,http,https,tcp,udp,rtp,rtmp,rtmps,rtsp,tls,srtp,crypto,data,pipe"
MINIMUM_DEMUXERS = "mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3"
MINIMUM_MUXERS = "mp4,mov,flv,mpegts,matroska,adts"

FLAVORS = ("gpl", "lgpl", "minsize", "minsize-gpl")


def build_options(flavor):
    opts = list(COMMON_OPTIONS)
    if flavor.startswith("minsize"):
        encoders = MINIMUM_ENCODERS
        if flavor == "minsize-gpl":
            # AGPL 白名单版: libx264/libx265 编码器需随 GPL 库显式开启
            encoders += ",libx264,libx265"
        opts += [
            "--disable-everything",
            f"--enable-protocol={MINIMUM_PROTOCOLS}",
            f"--enable-demuxer={MINIMUM_DEMUXERS}",
            f"--enable-muxer={MINIMUM_MUXERS}",
            f"--enable-decoder={MINIMUM_DECODERS}",
            f"--enable-encoder={encoders}",
            f"--enable-parser={MINIMUM_PARSERS}",
            f"--enable-bsf={MINIMUM_BSF}",
            f"--enable-hwaccel={MINIMUM_HWACCELS}",
        ]
    if flavor in ("gpl", "minsize-gpl"):
        opts += ["--enable-gpl", "--enable-libx264", "--enable-libx265"]
    return opts


def flavor_channel(flavor):
    return "AGPL" if flavor in ("gpl", "minsize-gpl") else "LGPL"


def configure_and_build(flavor, jobs):
    prefix = os.environ.get(
        "FFMPEG_PREFIX", os.path.join("..", "build", "windows", f"ffmpeg-{flavor}"))
    src_cwd = os.getcwd().replace("\\", "/")
    options = " ".join(build_options(flavor))
    script = (f'export PATH=/mingw64/bin:/usr/bin:$PATH; '
              f'cd "{src_cwd}" && ./configure --prefix="{prefix}" {options} '
              f'&& make -j{jobs} && make install -j{jobs}')
    print(f"[{flavor}/{flavor_channel(flavor)}] configure+make, prefix={prefix}")
    result = subprocess.run([bash_path, "-lc", script])
    if result.returncode != 0:
        print(f"构建失败 (exit {result.returncode})")
        sys.exit(result.returncode)
    print(f"ffmpeg-{flavor} 构建完成, 安装树: {prefix}")
    print(f"渠道校验: python {os.path.basename(__file__)} --verify \"{prefix}/bin\"")


def verify_dist(path):
    """扫描 dll/exe 内嵌 configure 串, 出现 GPL/Nonfree 标记即失败"""
    targets = []
    if os.path.isfile(path):
        targets.append(path)
    else:
        for name in sorted(os.listdir(path)):
            if name.lower().endswith((".dll", ".exe")):
                targets.append(os.path.join(path, name))
    bad = False
    for target in targets:
        with open(target, "rb") as f:
            blob = f.read()
        hits = [mark.decode() for mark in GPL_MARKS if mark in blob]
        if hits:
            bad = True
            print(f"❌ {os.path.basename(target)}: {', '.join(hits)}")
        else:
            print(f"✅ {os.path.basename(target)}")
    if bad:
        print("发现 GPL/Nonfree 标记: 该产物不可进商业(LGPL)渠道")
        sys.exit(1)
    print("verify 通过")


def main():
    args = sys.argv[1:]
    if args and args[0] == "--verify":
        if len(args) < 2:
            print("用法: python build_ffmpeg.py --verify <dll或目录>")
            sys.exit(2)
        verify_dist(args[1])
        return
    flavor = "minsize"
    for i, a in enumerate(args):
        if a == "--flavor" and i + 1 < len(args):
            flavor = args[i + 1]
        elif a.startswith("--flavor="):
            flavor = a.split("=", 1)[1]
    if flavor not in FLAVORS:
        print(f"未知 flavor: {flavor} (可选: {'/'.join(FLAVORS)})")
        sys.exit(2)
    configure_and_build(flavor, jobs=os.cpu_count() or 4)


if __name__ == "__main__":
    main()
