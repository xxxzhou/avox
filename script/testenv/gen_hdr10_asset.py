#!/usr/bin/env python3
# HDR10 测试素材生成 (PQ + BT.2020 + mastering SEI), 不依赖 GPL 编码器:
#   1. lavfi testsrc2 -> zscale 抬到 1000nit 域 -> BT.2020/PQ -> hevc_qsv Main10 (AnnexB)
#      (必须 -pix_fmt p010le, yuv420p10le 自动上传路径 QSV 报 Function not implemented)
#   2. 手工注入 SEI 137 (mastering display) + 144 (CLL) NAL (ffmpeg<7.1 无 hevc_metadata
#      master_display 选项; SEI 在首个 IRAP 前插入, 带 emulation prevention)
#   3. remux mp4 + aac
# 用法: python script/testenv/gen_hdr10_asset.py [输出.mp4] [--size 640x360] [--secs 10]
#       加 --sdr-only 只出 SDR 参考素材 (bt709 8bit, 供 tone map 对比)
#       加 --aud 在 SEI 前再注入 AUD, 验证 [AUD][SEI][IDR] 布局元数据不丢
# 判定: 末尾打印 case=genhdr10 PASS/FAIL (ffprobe 复核 transfer/primaries/side data)
import struct
import subprocess
import sys
import tempfile
import os

FFMPEG = "ffmpeg"
FFPROBE = "ffprobe"


def run(cmd):
    print("[cmd]", " ".join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout[-2000:] + r.stderr[-4000:])
        fail("命令失败: " + cmd[0])


def fail(reason):
    print(f"case=genhdr10 FAIL reason: {reason}")
    sys.exit(1)


def ep(rbsp):
    # emulation prevention: 00 00 0x (x<=3) 中插 03
    out = bytearray()
    zeros = 0
    for b in rbsp:
        if zeros >= 2 and b <= 3:
            out.append(3)
            zeros = 0
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def make_hdr_sei(maxCll=1000, maxFall=400):
    # SEI 137 mastering display: G(0.265,0.69) B(0.15,0.06) R(0.68,0.32)
    #   WP(0.3127,0.329) (x,y 乘 50000), 10000nit/0.0001nit (乘 10000)
    mastering = struct.pack(">6H", 13250, 34500, 7500, 3000, 34000, 16000)
    mastering += struct.pack(">2H", 15635, 16450)
    mastering += struct.pack(">II", 10000000, 1)
    cll = struct.pack(">2H", maxCll, maxFall)
    rbsp = bytes([137, len(mastering)]) + mastering
    rbsp += bytes([144, len(cll)]) + cll + b"\x80"
    return b"\x00\x00\x00\x01" + b"\x4e\x01" + ep(rbsp)  # prefix SEI nal(39)


def make_aud():
    # H265 AUD_NUT(35): 头 46 01 + pic_type=2(I)+rbsp trailing = 50
    return b"\x00\x00\x00\x01" + b"\x46\x01\x50"


def inject_hdr_sei(data, aud=False):
    sei = make_hdr_sei()
    if aud:
        # [AUD][SEI][IDR] 布局: AUD 在源码 AVSource 合并循环里曾是组起点,
        # 会把并进来的 SEI 一起丢掉 (singleVideo 按首 NAL=AUD 整组丢弃)
        sei = make_aud() + sei
    i, ins = 0, -1
    while True:
        j = data.find(b"\x00\x00\x01", i)
        if j < 0:
            break
        start = j + 3
        nalType = (data[start] & 0x7E) >> 1
        if nalType in (19, 20, 21):  # IRAP: IDR_W/IDR_N/CRA
            ins = j - 1 if j > 0 and data[j - 1] == 0 else j
            break
        i = start
    if ins < 0:
        fail("未找到 IRAP NAL")
    return data[:ins] + sei + data[ins:]


def probe(path):
    r = subprocess.run(
        [FFPROBE, "-v", "quiet", "-print_format", "json",
         "-show_streams", "-select_streams", "v", path],
        capture_output=True, text=True)
    import json
    return json.loads(r.stdout)["streams"][0]


def main():
    args = sys.argv[1:]
    out = args[0] if args and not args[0].startswith("-") else \
        "assets/video/test/test_h265_hdr10_pq_640x360.mp4"
    size, secs, sdrOnly, withAud = "640x360", "10", False, False
    for i, a in enumerate(args):
        if a == "--size":
            size = args[i + 1]
        elif a == "--secs":
            secs = args[i + 1]
        elif a == "--sdr-only":
            sdrOnly = True
        elif a == "--aud":
            withAud = True
    tmp = tempfile.mkdtemp(prefix="genhdr10_")
    base = os.path.splitext(out)[0]
    if withAud:
        out = base + "_aud.mp4"
    # SDR 参考 (同构图 bt709 8bit): tone map 结果对比基准
    sdr = base.replace("_hdr10_pq", "_sdr") + ".mp4"
    run([FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
         "-f", "lavfi", "-i", f"testsrc2=size={size}:rate=30",
         "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
         "-t", secs, "-pix_fmt", "nv12", "-c:v", "hevc_qsv", "-profile:v", "main",
         "-global_quality", "26",
         "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
         "-c:a", "aac", "-b:a", "96k", "-movflags", "+faststart", sdr])
    st = probe(sdr)
    if st.get("profile") != "Main" or \
            st.get("color_transfer") != "bt709":
        fail("SDR 参考素材标签异常: " + str(st.get("profile")))
    print(f"[ok] SDR 参考: {sdr}")
    if sdrOnly:
        print("case=genhdr10 PASS")
        return
    # HDR10: 素材内容保持 SDR 采样值 (本机 zscale/libplacebo 均无法完成 transfer
    # 域转换), 靠 VUI+SEI 标签声明 PQ/BT.2020 —— SDR 值按 PQ 解码中间调仍在
    # SDR 白附近, 高光 (码值 1.0) 落 10000nit, 足以触发/验证 tone map 压制。
    # qsv Main10 只吃 p010le 且带滤镜图上传会 -22, 两步: swscale 出 p010 裸
    # 文件, 再 rawvideo 直进编码器 (无滤镜链)
    raw10 = os.path.join(tmp, "raw.p010")
    run([FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
         "-f", "lavfi", "-i", f"testsrc2=size={size}:rate=30",
         "-t", secs,
         "-pix_fmt", "p010le", "-f", "rawvideo", raw10])
    annexb = os.path.join(tmp, "raw.265")
    run([FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
         "-f", "rawvideo", "-pix_fmt", "p010le",
         "-video_size", size, "-framerate", "30", "-i", raw10,
         "-c:v", "hevc_qsv", "-profile:v", "main10", "-global_quality", "26",
         "-color_primaries", "bt2020", "-color_trc", "smpte2084",
         "-colorspace", "bt2020nc", "-color_range", "tv",
         "-f", "hevc", annexb])
    with open(annexb, "rb") as f:
        data = f.read()
    if not data:
        fail("qsv AnnexB 输出为空")
    data = inject_hdr_sei(data, aud=withAud)
    with open(annexb, "wb") as f:
        f.write(data)
    run([FFMPEG, "-y", "-hide_banner", "-loglevel", "error",
         "-f", "hevc", "-i", annexb,
         "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
         "-c:v", "copy", "-c:a", "aac", "-b:a", "96k", "-shortest",
         "-color_primaries", "bt2020", "-color_trc", "smpte2084",
         "-colorspace", "bt2020nc", "-color_range", "tv",
         "-movflags", "+faststart", out])
    st = probe(out)
    ok = (st.get("profile") == "Main 10" and
          st.get("color_transfer") == "smpte2084" and
          st.get("color_primaries") == "bt2020")
    # 带内 SEI 在 mp4 中不出现在 stream side data; mp4 NAL 是长度前缀,
    # start code 被替换, 故搜去掉 start code 的 NAL 内容 (与注入同构造)
    with open(out, "rb") as f:
        mp4 = f.read()
    seiNal = make_hdr_sei()[4:]
    hasMd = hasCll = seiNal in mp4
    hasAud = (not withAud) or make_aud()[4:] in mp4
    print(f"[probe] profile={st.get('profile')} trc={st.get('color_transfer')} "
          f"prim={st.get('color_primaries')} space={st.get('color_space')} "
          f"pix={st.get('pix_fmt')} masteringSei={hasMd} cllSei={hasCll} "
          f"aud={hasAud}")
    if not ok:
        fail("HDR10 标签异常")
    if not (hasMd and hasCll and hasAud):
        fail("SEI/AUD 未注入成功 (成品流中未见对应 NAL)")
    print(f"case=genhdr10 PASS out={out}")


if __name__ == "__main__":
    main()
