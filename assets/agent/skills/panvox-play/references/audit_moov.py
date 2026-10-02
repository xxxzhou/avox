#!/usr/bin/env python3
"""audit_moov.py — 远程/本地 mp4 音轨审计(无声/坏片排查用)

用法:
  python audit_moov.py <url或本地路径> [--dump-audio t1,t2,...]

做两件事:
  1) walk 顶层 box 与 moov 内两条 trak 的 stbl(stsz/stco/stsc/stsd/mdhd), 打印
     样本数/大小分布/首末 offset —— 判断「假片/部分下载/空帧/锚文件头」族;
  2) --dump-audio: 按时点抓具体音频样本字节, 打 size/零字节数/唯一值数/RLE 游程,
     判「真数据 vs 填充/全零」(填充帧=长游程+低唯一值, 真 AAC=高熵)。

Windows 上 urllib 走系统代理对 LAN 恒 502, 统一用 curl --noproxy '*' 取 Range。
moov 可能在文件尾(非 faststart): 顶层 walk 到 mdat 后按 Content-Length 取尾。
凭据只从 history.json 读, 不打印 URL(红线: sources.json/URL 凭据不进结论)。
"""
import hashlib
import io
import json
import os
import re
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def fetch(url, a, b, local=None):
    if local:
        with open(local, 'rb') as f:
            f.seek(a)
            return f.read(b - a + 1)
    r = subprocess.run(['curl', '-sS', '--noproxy', '*', '-r', '%d-%d' % (a, b), url],
                       capture_output=True)
    if r.returncode != 0:
        sys.exit('curl rc=%d' % r.returncode)
    return r.stdout


def u32(b, o):
    return struct.unpack('>I', b[o:o + 4])[0]


def u64(b, o):
    return struct.unpack('>Q', b[o:o + 8])[0]


def box_at(b, off):
    size, typ = u32(b, off), b[off + 4:off + 8].decode('latin1')
    hdr = 8
    if size == 1:
        size = u64(b, off + 8); hdr = 16
    elif size == 0:
        size = len(b) - off
    return typ, size, hdr


def children(b, start, end):
    p, out = start, []
    while p + 8 <= end:
        typ, size, hdr = box_at(b, p)
        if size < hdr:
            break
        out.append((typ, p, size, hdr))
        p += size
    return out


def main():
    url = sys.argv[1]
    if url.startswith('hist:'):  # hist:<history.json 键> 免凭据上命令行
        hist = json.load(io.open(os.path.join(os.environ['APPDATA'], 'panvox', 'history.json'),
                                 encoding='utf-8'))
        url = hist[url[5:]]['url']
    local = url if os.path.exists(url) else None
    dump = None
    if '--dump-audio' in sys.argv:
        dump = [float(x) for x in sys.argv[sys.argv.index('--dump-audio') + 1].split(',')]

    if local:
        total = os.path.getsize(local)
        head = fetch(url, 0, 65535, local)
    else:
        hd = subprocess.run(['curl', '-sSI', '--noproxy', '*', url], capture_output=True, text=True).stdout
        m = re.search(r'(?im)^content-length:\s*(\d+)', hd)
        total = int(m.group(1)) if m else None
        head = fetch(url, 0, 65535)
    print('TOTAL SIZE:', total)

    pos, tops = 0, []
    while pos + 8 <= len(head):
        typ, size, hdr = box_at(head, pos)
        tops.append((typ, pos, size))
        if typ in ('moov', 'mdat') or size == 0:
            break
        pos += size
    print('top boxes:', [(t, s) for t, _, s in tops])
    moo = next(((p, s) for t, p, s in tops if t == 'moov'), None)
    if moo:
        moov = fetch(url, moo[0], moo[0] + moo[1] - 1, local)
        mbase = 0
    else:
        end = sum(s for _, _, s in tops)
        print('moov at tail:', end, 'size', total - end)
        moov = fetch(url, end, total - 1, local)
        mbase = end
    print('moov bytes:', len(moov))

    tracks = []
    audio_sr = 48000
    for typ, p, sz, h in children(moov, 8, len(moov)):
        if typ != 'trak':
            continue
        mdia = next(x for x in children(moov, p + h, p + sz) if x[0] == 'mdia')
        mk = children(moov, mdia[1] + mdia[3], mdia[1] + mdia[2])
        hdlr = next(x for x in mk if x[0] == 'hdlr')
        htype = moov[hdlr[1] + hdlr[3] + 8:hdlr[1] + hdlr[3] + 12].decode('latin1')
        mdhd = next(x for x in mk if x[0] == 'mdhd')
        o = mdhd[1] + mdhd[3]
        ts, dur = struct.unpack('>II', moov[o + 12:o + 20])
        minf = next(x for x in mk if x[0] == 'minf')
        stbl = next(x for x in children(moov, minf[1] + minf[3], minf[1] + minf[2]) if x[0] == 'stbl')
        d = {t: (o2, s2, h2) for t, o2, s2, h2 in children(moov, stbl[1] + stbl[3], stbl[1] + stbl[2])}
        print('--- trak handler=%s timescale=%d dur=%.1fs boxes=%s' % (
            htype, ts, dur / ts, sorted(d)))
        if htype == 'soun':
            audio_sr = ts
        if 'stsd' in d:
            o2 = d['stsd'][0] + d['stsd'][2] + 8
            esz, fmt = u32(moov, o2), moov[o2 + 4:o2 + 8].decode('latin1')
            extra = ''
            if fmt == 'mp4a':
                ch = struct.unpack('>H', moov[o2 + 24:o2 + 26])[0]
                sr = u32(moov, o2 + 32) >> 16
                extra = ' stsd_ch=%d stsd_sr=%d(注: mp4 常写 2ch, 以 esds/ASC 为准)' % (ch, sr)
            print('  stsd fmt=%s size=%d%s' % (fmt, esz, extra))
        if 'stsz' in d:
            o2 = d['stsz'][0] + d['stsz'][2]
            fixed, cnt = u32(moov, o2 + 4), u32(moov, o2 + 8)
            sizes = [fixed] * cnt if fixed else list(
                struct.unpack('>%dI' % cnt, moov[o2 + 12:o2 + 12 + 4 * cnt]))
            print('  stsz n=%d min=%d max=%d avg=%.1f sum=%.2fMB' % (
                cnt, min(sizes), max(sizes), sum(sizes) / max(cnt, 1), sum(sizes) / 1e6))
            print('  head16=%s tail8=%s' % (sizes[:16], sizes[-8:]))
        tables = {}
        if 'stts' in d:
            o2 = d['stts'][0] + d['stts'][2]
            tables['stts'] = [struct.unpack('>II', moov[o2 + 8 + i * 8:o2 + 16 + i * 8])
                              for i in range(min(u32(moov, o2 + 4), 4))]
        if 'stco' in d:
            o2 = d['stco'][0] + d['stco'][2]
            n = u32(moov, o2 + 4)
            tables['stco'] = [u32(moov, o2 + 8 + i * 4) for i in range(n)]
        if 'stsc' in d:
            o2 = d['stsc'][0] + d['stsc'][2]
            n = u32(moov, o2 + 4)
            tables['stsc'] = [struct.unpack('>III', moov[o2 + 8 + i * 12:o2 + 20 + i * 12])
                              for i in range(n)]
        tracks.append((htype, tables, sizes))

    av = next(t for t in tracks if t[0] == 'soun')
    if dump:
        _, tb, sizes = av
        chunk_off, rows = tb['stco'], tb['stsc']
        cfs, si = [], 0
        for i, (fc, spc, _) in enumerate(rows):
            nf = rows[i + 1][0] if i + 1 < len(rows) else len(chunk_off) + 1
            for _c in range(fc, nf):
                cfs.append((si, spc)); si += spc

        def sample_off(idx):
            lo, hi = 0, len(cfs) - 1
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if cfs[mid][0] <= idx:
                    lo = mid
                else:
                    hi = mid - 1
            first_si, _ = cfs[lo]
            return chunk_off[lo] + sum(sizes[first_si:idx])

        print('== audio samples (sr=%d, t -> idx=floor(t*sr/1024)) ==' % audio_sr)
        for t in dump:
            idx = int(t * audio_sr / 1024)
            if idx >= len(sizes):
                continue
            off = sample_off(idx)
            nb = fetch(url, off, off + sizes[idx] - 1, local)
            runs = {}
            i = 0
            while i < len(nb):
                j = i
                while j < len(nb) and nb[j] == nb[i]:
                    j += 1
                runs[bytes([nb[i]]).hex()] = max(runs.get(bytes([nb[i]]).hex(), 0), j - i)
                i = j
            top = sorted(runs.items(), key=lambda kv: -kv[1])[:3]
            print('t=%7.1fs idx=%6d off=%9d size=%4d zeros=%3d uniq=%3d md5=%s topRLE=%s head=%s' % (
                t, idx, off, sizes[idx], nb.count(0), len(set(nb)),
                hashlib.md5(nb).hexdigest()[:8], top, nb[:12].hex(' ')))


if __name__ == '__main__':
    main()
