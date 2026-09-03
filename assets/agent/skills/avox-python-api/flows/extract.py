# flows/extract.py — 帧与音频提取 (ISurfaceRenderOb 取帧 + IAudioRender 取 WAV)
#
# 入口: run(src, out_dir, source='media', ...) → 结构化 dict。
# 从三种宿主分别测试视频帧提取 (两种方式) 与音频 WAV 提取:
#   source='media'    : IMediaPlayer 播放 URL/本地文件
#   source='source'   : ISourcePlayer 采集设备 (默认桌面 + 麦)
#   source='recorder' : IRecorder 的转码模式 (createRecorder(True), src → mp4); IRecorder 另有原样保存/离线处理两用法, 见源码 docstring
#
# 视频两种取帧方式 (均经 ISurfaceRender):
#   'yuv'        : ISurfaceRenderOb.onFrame(YUVFrame) → Image.yuvframe2Rgba → IImageBuffer → 存 PNG
#                  (onFrame 即 ISurfaceRenderOb 的帧回调; 需先 enableYuvOut/setOffSurface 打开 YUV 输出)
#   'screenshot' : ISurfaceRender.screenShotToPath 定时拉取当前渲染帧
# 音频:
#   'wav'        : IAudioRender.startWavRecord 录指定时长 → WAV 文件
#
# 无窗口 (run_code) 场景取帧 (均经 setOffSurface 离屏):
#   - onFrame 取 YUV: setOffSurface(yuv420P) 直接输出 YUV。
#   - screenShot 取帧: 播放器需 setOffSurface(YuvType.other) 切到"渲染目标"模式 (yuv420P 时 screenShot 不可读);
#     录制器的编码目标本身可读, 保持 yuv420P 即可。
# (enableYuvOut 是配合 setSurface 用的: setSurface 默认只 GPU 输出到窗口, enableYuvOut 才额外开 YUV 帧。)
# 三种宿主、两条视频链路各自独立报告成败。
#
# 返回 dict: {ok, source, yuv:{ok,frames,errors}, screenshot:{ok,frames,errors},
#             wav:{ok,path,duration,errors}, recorderOutput, errors}
# 调用方按 result['yuv'/'screenshot'/'wav']['ok'] 判断每条链路; errors 汇总全部问题。

import os
import time
import threading

from avox import Player, Source, Muxer, Image
from avox._core import (MuxerType, VCodecId, ACodecId, PlayerState, RecorderState,
                       YuvType)
from flows.common import pick_video_device, pick_audio_device, make_logger


# ─── 宿主打开 (返回 ctx 或 None) ─────────────────────────

def _open_media(src, out_dir, errors, log):
    """IMediaPlayer: 打开 URL/本地文件, 离屏渲染。"""
    if not src:
        errors.append('media 模式需要 src (URL/本地文件路径)')
        return None
    p = Player.IMediaPlayer()
    ready = threading.Event()
    p.onReady(lambda: ready.set())
    p.onStateChange(lambda pre, s: ready.set() if s in (PlayerState.ready, PlayerState.playing) else None)
    p.open(src)
    if not ready.wait(timeout=12):
        errors.append(f'media 未就绪 (state={p.state})')
        try:
            p.close()
        except Exception:
            pass
        return None
    sr = p.getSurfaceRender()
    log(f'media 已就绪: {src}')
    return {'kind': 'media', 'host': p, 'sr': sr, 'ar': p.getAudioRender()}


def _open_source(src, out_dir, errors, log):
    """ISourcePlayer: 采集设备 (桌面 + 麦), 离屏渲染。"""
    p = Player.ISourcePlayer()
    vn = pick_video_device('desktop', errors)
    an = pick_audio_device('mic', errors)
    if vn:
        p.setVideoSource(vn)
    if an:
        p.setAudioSource(an)
    ready = threading.Event()
    p.onReady(lambda: ready.set())
    p.onStateChange(lambda pre, s: ready.set() if s in (PlayerState.ready, PlayerState.playing) else None)
    p.open()
    if not ready.wait(timeout=12):
        errors.append(f'source 未就绪 (state={p.state})')
        try:
            p.close()
        except Exception:
            pass
        return None
    sr = p.getSurfaceRender()
    log('source 已就绪 (设备采集)')
    return {'kind': 'source', 'host': p, 'sr': sr, 'ar': p.getAudioRender()}


def _open_recorder(src, out_dir, errors, log):
    """IRecorder 的转码模式 (createRecorder(True)): 边转码 src → mp4, 边从其 SurfaceRender/AudioRender 取帧与音频。
    IRecorder 另有原样保存(remux)/离线处理(out 空)两种用法, 见源码 docstring。"""
    if not src:
        errors.append('recorder 模式需要 src (输入 URL/文件)')
        return None
    output = os.path.join(out_dir, 'recorder_output.mp4')
    rec = Muxer.createRecorder(bTranscode=True)
    rec.setMuxerType(MuxerType.ffmpeg)
    rec.setVideoCodec(VCodecId.h264)
    rec.setAudioCodec(ACodecId.aac)
    ready = threading.Event()
    rec.onProgress(lambda prog: ready.set())
    if not rec.open(src, output):
        errors.append(f'recorder.open 失败: {src} → {output}')
        try:
            rec.destroy()
        except Exception:
            pass
        return None
    if not ready.wait(timeout=12):
        errors.append(f'recorder 未进入录制 (state={rec.state})')
        try:
            rec.destroy()
        except Exception:
            pass
        return None
    sr = rec.getSurfaceRender()
    if sr:
        sr.setOffSurface(YuvType.yuv420P)
    ar = rec.getAudioRender()
    log(f'recorder 转码中: {src} → {output}')
    return {'kind': 'recorder', 'host': rec, 'sr': sr, 'ar': ar, 'output': output}


_OPENERS = {'media': _open_media, 'source': _open_source, 'recorder': _open_recorder}


# ─── 三条提取链路 ────────────────────────────────────────

def _sample_yuv(sr, out_dir, tag, interval, count, errors, log, kind):
    """方式A: onFrame(YUVFrame) → yuvframe2Rgba → IImageBuffer → 存 PNG。
    onFrame 在渲染线程触发; 仅做一次 (C++) 转换并暂存 RGBA 拷贝, 存盘放到主线程。"""
    res = {'ok': False, 'frames': [], 'errors': []}
    if sr is None:
        res['errors'].append('无 SurfaceRender (此宿主不支持 onFrame 取帧)')
        return res
    if kind != 'recorder':
        sr.setOffSurface(YuvType.yuv420P)
    st = {'last': 0.0, 'i': 0, 'bufs': []}

    def on_frame(yuvFrame):
        if st['i'] >= count:
            return
        now = time.time()
        if st['i'] > 0 and now - st['last'] < interval:
            return
        st['last'] = now
        try:
            buf = Image.IImageBuffer()
            if Image.yuvframe2Rgba(yuvFrame, buf):
                st['bufs'].append(buf)
                st['i'] += 1
        except Exception as e:
            res['errors'].append(f'yuv 转换: {e}')

    try:
        sr.onFrame(on_frame)
        t0 = time.time()
        while st['i'] < count and time.time() - t0 < count * interval + 6:
            time.sleep(0.1)
    except Exception as e:
        res['errors'].append(f'yuv onFrame: {e}')
    for i, buf in enumerate(st['bufs']):
        path = os.path.join(out_dir, f'{tag}_yuv_{i}.png')
        try:
            Image.saveImage(path, buf)
            res['frames'].append(path)
        except Exception as e:
            res['errors'].append(f'yuv 存盘: {e}')
    res['ok'] = len(res['frames']) > 0
    errors.extend(res['errors'])
    log(f"[yuv] {len(res['frames'])}/{count} 帧")
    return res


def _sample_screenshot(sr, out_dir, tag, interval, count, errors, log, kind):
    """方式B: 定时 screenShotToPath 拉取当前渲染帧。"""
    res = {'ok': False, 'frames': [], 'errors': []}
    if sr is None:
        res['errors'].append('无 SurfaceRender (此宿主不支持 screenShot)')
        return res
    if kind != 'recorder':
        sr.setOffSurface(YuvType.other)
        time.sleep(0.4)
    for i in range(count):
        path = os.path.join(out_dir, f'{tag}_shot_{i}.png')
        try:
            ok = sr.screenShotToPath(path)
            if ok and os.path.exists(path) and os.path.getsize(path) > 0:
                res['frames'].append(path)
            else:
                res['errors'].append(f'shot {i}: screenShot 返回 False (可能无渲染目标/未到首帧)')
        except Exception as e:
            res['errors'].append(f'shot {i}: {e}')
        if i < count - 1:
            time.sleep(interval)
    res['ok'] = len(res['frames']) > 0
    errors.extend(res['errors'])
    log(f"[screenshot] {len(res['frames'])}/{count} 帧")
    return res


def _record_wav(ar, out_dir, tag, duration, errors, log):
    """录一段音频到 WAV: startWavRecord 自动 openTap + IWavSave + 注册回调。"""
    res = {'ok': False, 'path': '', 'duration': 0.0, 'errors': []}
    if ar is None:
        res['errors'].append('无 AudioRender (此宿主不支持音频 tap; 如 StreamRecorder 返回 None)')
        return res
    path = os.path.join(out_dir, f'{tag}_audio.wav')
    try:
        ar.startWavRecord(path)
        t0 = time.time()
        while time.time() - t0 < duration:
            time.sleep(0.1)
        res['duration'] = time.time() - t0
        ar.stopWavRecord()
        if os.path.exists(path) and os.path.getsize(path) > 0:
            res['path'] = path
            res['ok'] = True
        else:
            res['errors'].append('WAV 未生成 (源可能无音频轨)')
    except Exception as e:
        res['errors'].append(f'wav: {e}')
        try:
            ar.stopWavRecord()
        except Exception:
            pass
    errors.extend(res['errors'])
    log(f"[wav] {'OK' if res['ok'] else '失败'} {path} ({res['duration']:.1f}s)")
    return res


# ─── 入口 ────────────────────────────────────────────────

def run(src=None, out_dir='.', source='media',
        interval=1.0, count=3, audio_dur=3.0,
        modes=('yuv', 'screenshot', 'wav'), verbose=True):
    """从 IMediaPlayer / ISourcePlayer / IRecorder(bTranscode) 提取视频帧 (两种) 与音频 WAV。

    参数:
      src        媒体 URL/本地路径 (media、recorder 必填; source 模式忽略)
      out_dir    输出目录 (帧 PNG + WAV + recorder 的 mp4)
      source     宿主: 'media'(默认) / 'source' / 'recorder'
      interval   视频取帧间隔(秒), 默认 1.0
      count      每种视频方式取帧数, 默认 3
      audio_dur  WAV 录音时长(秒), 默认 3.0
      modes      启用的提取项, ('yuv','screenshot','wav') 的子集; 默认全开。也接受单字符串
      verbose    打印进度到 stdout

    返回 dict:
      {ok, source,
       yuv:        {ok, frames:[...], errors:[...]},
       screenshot: {ok, frames:[...], errors:[...]},
       wav:        {ok, path, duration, errors:[...]},
       recorderOutput: str,   # 仅 source='recorder' 时的转码输出 mp4 路径
       errors:[...]}          # 汇总所有链路的错误
    ok=True 表示至少一条链路成功; 各链路成败看对应子 dict 的 ok。
    """
    if isinstance(modes, str):
        modes = (modes,)
    result = {'ok': False, 'source': source,
              'yuv': {'ok': False, 'frames': [], 'errors': []},
              'screenshot': {'ok': False, 'frames': [], 'errors': []},
              'wav': {'ok': False, 'path': '', 'duration': 0.0, 'errors': []},
              'recorderOutput': '', 'errors': []}
    errs = result['errors']
    log = make_logger(verbose)

    if not out_dir:
        errs.append('out_dir 为空')
        return result
    os.makedirs(out_dir, exist_ok=True)

    opener = _OPENERS.get(source)
    if opener is None:
        errs.append(f"source 需为 'media'/'source'/'recorder', 得到 {source!r}")
        return result

    ctx = None
    try:
        ctx = opener(src, out_dir, errs, log)
        if ctx is None:
            return result
        sr, ar = ctx['sr'], ctx['ar']
        result['recorderOutput'] = ctx.get('output', '')
        if 'yuv' in modes:
            result['yuv'] = _sample_yuv(sr, out_dir, source, interval, count, errs, log, source)
        if 'screenshot' in modes:
            result['screenshot'] = _sample_screenshot(sr, out_dir, source, interval, count, errs, log, source)
        if 'wav' in modes:
            result['wav'] = _record_wav(ar, out_dir, source, audio_dur, errs, log)
        result['ok'] = result['yuv']['ok'] or result['screenshot']['ok'] or result['wav']['ok']
    except Exception as e:
        errs.append(f'extract: {e}')
    finally:
        if ctx is not None:
            host = ctx.get('host')
            if ctx.get('kind') == 'recorder':
                try:
                    host.destroy()
                except Exception:
                    pass
            else:
                try:
                    host.close()
                except Exception:
                    pass

    log(f"=== {source} 完成: ok={result['ok']} "
        f"yuv={len(result['yuv']['frames'])} "
        f"shot={len(result['screenshot']['frames'])} "
        f"wav={result['wav']['ok']} ===")
    return result
