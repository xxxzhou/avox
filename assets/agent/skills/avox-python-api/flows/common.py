# flows/common.py — flows 共享工具
#
# 路径定位、设备选择、日志、SRT 读写等。
# 各 flow: from flows.common import assets_path, make_logger, ...

import os
import re

from avox import Source
from avox._core import VDeviceKind, ADeviceKind


# ─── 路径定位 ──────────────────────────────────────────────

# skill 根目录: 本文件在 flows/ 下, 上级即 skill 根 (含 assets/flows/SKILL.md)
_SKILL_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_ASSETS_DIR = os.path.join(_SKILL_DIR, 'assets')


def assets_path(name):
    """返回 skill 内 assets/ 目录下文件的绝对路径。

    供模板匹配 tmpl_path、OCR 测试图等用:
        from flows.common import assets_path
        tmpl = assets_path('icon_red.png')

    也可直接传绝对路径给各函数, 此时本函数不需要。
    """
    return os.path.join(_ASSETS_DIR, name)


def skill_dir():
    """返回 skill 根目录绝对路径 (含 assets/flows/SKILL.md)。"""
    return _SKILL_DIR


# ─── 设备选择 ──────────────────────────────────────────────

def pick_video_device(video, errors):
    """按参数选视频设备, 返回 IVideoSource.native 或 None。

    video: 'desktop' / 'window:标题子串' / 'none' / int(设备索引) / None(默认桌面)。
    """
    if video == 'none':
        return None
    vmgr = Source.getVideoManager()
    if video is None or video == 'desktop':
        for i in range(vmgr.getDeviceCount()):
            d = vmgr.getDevice(i)
            if d and d.getDeviceKind() == VDeviceKind.monitor:
                return d.native
        d = vmgr.getDevice(0)
        if d:
            return d.native
        errors.append('无视频设备')
        return None
    if isinstance(video, int):
        d = vmgr.getDevice(video)
        if d:
            return d.native
        errors.append(f'视频设备索引 {video} 越界')
        return None
    if isinstance(video, str) and video.startswith('window:'):
        title = video[len('window:'):]
        for i in range(vmgr.getDeviceCount()):
            d = vmgr.getDevice(i)
            if d and d.getDeviceKind() == VDeviceKind.window and title in d.getDeviceName():
                return d.native
        errors.append(f'未找到窗口含 "{title}"')
        return None
    errors.append(f'video 参数不识别: {video}')
    return None


def pick_audio_device(audio, errors):
    """按参数选音频设备, 返回 IAudioSource.native 或 None。

    audio: 'mic' / 'loopback' / 'none' / int(设备索引) / None(默认麦)。
    """
    if audio == 'none':
        return None
    amgr = Source.getAudioManager()
    if audio is None or audio == 'mic':
        for i in range(amgr.getDeviceCount()):
            d = amgr.getDevice(i)
            if d and d.getDeviceKind() == ADeviceKind.mic:
                return d.native
        d = amgr.getDevice(0)
        if d:
            return d.native
        errors.append('无音频设备')
        return None
    if audio == 'loopback':
        for i in range(amgr.getDeviceCount()):
            d = amgr.getDevice(i)
            if d and d.getDeviceKind() == ADeviceKind.loopback:
                return d.native
        errors.append('无声卡回环设备 (loopback)')
        return None
    if isinstance(audio, int):
        d = amgr.getDevice(audio)
        if d:
            return d.native
        errors.append(f'音频设备索引 {audio} 越界')
        return None
    errors.append(f'audio 参数不识别: {audio}')
    return None


# ─── 日志 ──────────────────────────────────────────────────

def make_logger(verbose):
    """返回 log(msg) 闭包; verbose=True 时 print, 否则静默。"""
    if verbose:
        return lambda msg: print(msg)
    return lambda msg: None


# ─── SRT 读写 ──────────────────────────────────────────────

def fmt_time_ms(ms):
    """毫秒 → SRT 时间戳 'HH:MM:SS,mmm'。"""
    if not ms or ms < 0:
        ms = 0
    ms = int(ms)
    h, ms = divmod(ms, 3600000)
    m, ms = divmod(ms, 60000)
    s, ms = divmod(ms, 1000)
    return f'{h:02d}:{m:02d}:{s:02d},{ms:03d}'


def parse_srt_time(t):
    """SRT 时间戳 'HH:MM:SS,mmm' → 毫秒。"""
    h, m, rest = t.split(':')
    s, ms = rest.split(',')
    return int(h) * 3600000 + int(m) * 60000 + int(s) * 1000 + int(ms)


def write_srt(path, segs):
    """写 SRT 文件。segs: list of (startMs, endMs, text)。"""
    with open(path, 'w', encoding='utf-8') as f:
        for i, (st, et, txt) in enumerate(segs, 1):
            f.write(f'{i}\n{fmt_time_ms(st)} --> {fmt_time_ms(et)}\n{txt}\n\n')


def parse_srt(path):
    """读 SRT 文件 → list of (startMs, endMs, text)。"""
    with open(path, encoding='utf-8') as f:
        content = f.read()
    segs = []
    for block in re.split(r'\r?\n\r?\n', content.strip()):
        lines = [l for l in block.splitlines() if l.strip()]
        if len(lines) < 3:
            continue
        m = re.match(r'(\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3})',
                     lines[1])
        if not m:
            continue
        segs.append((parse_srt_time(m.group(1)), parse_srt_time(m.group(2)),
                     '\n'.join(lines[2:]).strip()))
    return segs
