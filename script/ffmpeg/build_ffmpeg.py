import glob
import os
import shutil
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
# 2026-09 NAS 实测扩展: minsize 白名单补老媒体编解码(RMVB/WMV/MPG/老AVI/3GP 等),
#   demuxer +rm/mpegps/mpegvideo, decoder +mpeg1/2/4,h263,wmv1/2/3,vc1,flv1,rv10-40,
#   cook,sipr,atrac3,wmav1/2,wmapro,pcm_s16be, parser +mpeg4video,vc1; 全部 LGPL, 许可不变。
#
# 环境变量:
#   MSYS2_INSTALL_DIR  MSYS2 根目录 (默认 C:\msys64)
#   FFMPEG_PREFIX      安装树绝对路径 (默认 ../build/windows/ffmpeg-<flavor>)
#
# 用法 (在 FFmpeg 源码目录里跑):
#   python build_ffmpeg.py --flavor minsize
#   python build_ffmpeg.py --flavor minsize --deploy   # 构建后换库进 avox 3rdparty(旧库先备份)
#   python build_ffmpeg.py --verify <dll或目录>   # 扫描 configure 串, GPL/Nonfree 标记即报错

MSYS2_INSTALL_DIR = os.environ.get("MSYS2_INSTALL_DIR", "C:\\msys64")
# avox 链接目标库仓(与 cmake/FindFFmpeg.cmake 的 windows 路径一致); 脚本在 <avox>/script/ffmpeg/ 下
AVOX_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AVOX_FFMPEG_DIR = os.path.join(AVOX_ROOT, "3rdparty", "library", "windows", "ffmpeg")
# 随库分发的运行时 dll(mingw 依赖 zlib1/libwinpthread; ffmpeg 大版本升级 DLL 主版本号会变, 用通配)
DEPLOY_DLL_PATTERNS = ("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll",
                       "swresample-*.dll", "zlib1.dll", "libwinpthread-1.dll")
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
# 后半段(mpeg1video 起)为 2026-09 NAS 实测扩展的老媒体解码器, 见文件头说明
MINIMUM_DECODERS = ("h264,hevc,aac,mp3,opus,ac3,pcm_alaw,pcm_mulaw,pcm_s16le,pcm_s24le,"
                    "mpeg1video,mpeg2video,mpeg4,h263,flv1,"      # 老AVI/3GP/FLV/MPG
                    "wmv1,wmv2,wmv3,vc1,"                          # ASF/WMV
                    "rv10,rv20,rv30,rv40,"                         # RealVideo
                    "cook,sipr,atrac3,"                            # RealAudio
                    "wmav1,wmav2,wmapro,"                          # WMA
                    "pcm_s16be")                                   # MOV LPCM
# hwaccel 是 avcodec 独立组件, --disable-everything 会连它一起裁掉;
# 不显式加回则 ff_get_format 拿不到 D3D11 配置, 硬解逐帧静默回退软解 (09-10 排查结论)。
# 注意 FFmpeg9 拆了新旧两个组件: d3d11va(legacy, D3D11VA_VLD, 不支持 hw_device_ctx)
# 与 d3d11va2(现代, D3D11, 走 hw_device_ctx)—— 运行时走的是 d3d11va2。
# legacy 必须同开: 9.0.1 的 Makefile 只在 legacy/dxva2 配置下才编 dxva2_h264.o
# (d3d11va2 的符号也在这个文件里), 只开 d3d11va2 会链接失败 (09-10 实测)。
# legacy 运行时无害: 无 HW_DEVICE_CTX 方法, 默认选择器会自动跳过
MINIMUM_HWACCELS = "h264_d3d11va,h264_d3d11va2,hevc_d3d11va,hevc_d3d11va2"
MINIMUM_ENCODERS = "h264_mf,hevc_mf,aac"   # 商业渠道; h264_mf/hevc_mf 为系统自带 MFT
MINIMUM_PARSERS = "h264,hevc,aac,mp3,opus,ac3,mpegaudio,mpeg4video,vc1"  # 后两项为老媒体扩展(wmv3/vc1/mpeg4 帧内解析需要)
MINIMUM_BSF = "h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata"
# 直播/点播/文件: rtmp 系 + rtsp 系 + http(s) 系 + hls(crypto=AES 解密) + file
# 后续需要 SRT: 装 libsrt + --enable-libsrt --enable-protocol=srt
MINIMUM_PROTOCOLS = "file,http,https,tcp,udp,rtp,rtmp,rtmps,rtsp,tls,srtp,crypto,data,pipe"
# 后三项(rm,mpegps,mpegvideo)为老媒体扩展。注意: FFmpeg 新版 MPEG-PS demuxer 的
# configure 名是 mpegps(旧名 mpeg 已废弃, 传 mpeg 会被 configure 静默忽略不报错);
# mpegvideo 是裸 MPEG-1/2 ES 流(.mpg 探测失败时靠它兜底)
MINIMUM_DEMUXERS = ("mov,matroska,flv,live_flv,mpegts,hls,avi,asf,aac,mp3,ogg,wav,rtsp,sdp,ac3,"
                    "rm,mpegps,mpegvideo")
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
    # MSYSTEM=MINGW64: mingw64 登录环境的标准姿势, 保证 configure/make 工具链解析稳定
    script = (f'export PATH=/mingw64/bin:/usr/bin:$PATH; export MSYSTEM=MINGW64; '
              f'cd "{src_cwd}" && ./configure --prefix="{prefix}" {options} '
              f'&& make -j{jobs} && make install -j{jobs}')
    print(f"[{flavor}/{flavor_channel(flavor)}] configure+make, prefix={prefix}")
    result = subprocess.run([bash_path, "-lc", script])
    if result.returncode != 0:
        print(f"构建失败 (exit {result.returncode})")
        sys.exit(result.returncode)
    print(f"ffmpeg-{flavor} 构建完成, 安装树: {prefix}")
    print(f"渠道校验: python {os.path.basename(__file__)} --verify \"{prefix}/bin\"")
    return prefix


def deploy(prefix):
    """换库进 avox 3rdparty: 拷 install/bin 的 dll(旧库整目录一次性备份)。
    导入 .lib/include 与旧版同主版本时无需动; ffmpeg 大版本变化(如 63->64)时
    DLL 名会变, 需连同 include/ 与 .lib 一起同步并核对 FindFFmpeg.cmake。"""
    src = os.path.join(prefix, "bin")
    if not os.path.isdir(src):
        print("install/bin 不存在, 先执行不带 --deploy 的构建")
        sys.exit(1)
    if not os.path.isdir(AVOX_FFMPEG_DIR):
        print("目标不存在:", AVOX_FFMPEG_DIR)
        sys.exit(1)
    bak = AVOX_FFMPEG_DIR + "-bak"
    if not os.path.isdir(bak):
        shutil.copytree(AVOX_FFMPEG_DIR, bak)
        print("backup ->", bak)
    copied = []
    for pattern in DEPLOY_DLL_PATTERNS:
        for p in sorted(glob.glob(os.path.join(src, pattern))):
            shutil.copy2(p, os.path.join(AVOX_FFMPEG_DIR, "bin", os.path.basename(p)))
            copied.append(os.path.basename(p))
    if not copied:
        print("FAILED: install/bin 里没有任何可部署 dll")
        sys.exit(1)
    # 运行时依赖自检: 扫部署 dll 的导入表, 非 Windows 系统 dll 且目标 bin 缺失的,
    # 从 MSYS2 mingw64/bin 补拷(如 zlib1.dll/libwinpthread-1.dll)
    objdump = os.path.join(MSYS2_INSTALL_DIR, "mingw64", "bin", "objdump.exe")
    dest_bin = os.path.join(AVOX_FFMPEG_DIR, "bin")
    # 注: zlib 由 avformat 动态链(zlib1.dll), avcodec 不直接依赖
    SYSTEM_DLLS = {"kernel32.dll", "msvcrt.dll", "user32.dll", "ole32.dll", "oleaut32.dll",
                   "advapi32.dll", "ws2_32.dll", "bcrypt.dll", "crypt32.dll", "ncrypt.dll",
                   "secur32.dll", "ntdll.dll", "shell32.dll", "gdi32.dll"}
    for name in list(copied):
        info = subprocess.run([objdump, "-p", os.path.join(dest_bin, name)],
                              capture_output=True, text=True)
        for line in info.stdout.splitlines():
            dep = line.split("DLL Name:")[-1].strip().lower() if "DLL Name:" in line else ""
            if (dep and dep not in SYSTEM_DLLS and dep.endswith(".dll")
                    and dep not in [c.lower() for c in os.listdir(dest_bin)]):
                src_dep = os.path.join(MSYS2_INSTALL_DIR, "mingw64", "bin", dep)
                if os.path.isfile(src_dep):
                    shutil.copy2(src_dep, os.path.join(dest_bin, dep))
                    copied.append(dep + "(mingw64)")
                else:
                    print(f"WARN: 依赖 {dep} 不在 mingw64/bin, 需手工补")
    print("deploy ->", dest_bin, ":", ", ".join(copied))


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
    b_deploy = False
    for i, a in enumerate(args):
        if a == "--flavor" and i + 1 < len(args):
            flavor = args[i + 1]
        elif a.startswith("--flavor="):
            flavor = a.split("=", 1)[1]
        elif a == "--deploy":
            b_deploy = True
    if flavor not in FLAVORS:
        print(f"未知 flavor: {flavor} (可选: {'/'.join(FLAVORS)})")
        sys.exit(2)
    prefix = configure_and_build(flavor, jobs=os.cpu_count() or 4)
    if b_deploy:
        deploy(prefix)


if __name__ == "__main__":
    main()
