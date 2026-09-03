# flows/capture.py — 采集推流/录制 (桌面/窗口 + 麦/声卡 → 文件/推流)
#
# 入口: run(output, ...) 一步采集+输出, 返回结构化 dict。
# 支持:
#   视频源: 桌面 monitor / 指定窗口 (按标题子串)
#   音频源: 麦 mic / 声卡回环 loopback / 无
#   输出:   本地文件 (.mp4) / RTMP 推流 (rtmp://...)
#   处理:   可选叠水印/调节 (走 SurfaceRender, 仅 bTranscode=True 时进入输出文件)
#
# 采集由 ISourcePlayer 驱动 (有窗口上下文), 输出经 player.getMuxer()。
# 开/关由 Player 接管, 本模块不手动 open/close 设备源。

import os
import time

from avox import Player, Muxer
from avox._core import MuxerType, VCodecId, ACodecId, PlayerState
from flows.common import pick_video_device, pick_audio_device, make_logger


def _is_rtmp(url):
    return url and isinstance(url, str) and url.startswith('rtmp://')


# ─── 入口 ────────────────────────────────────────────────

def run(output, video='desktop', audio='mic',
        bTranscode=True, watermark=None, adjust=None,
        duration=0, verbose=True):
    """采集桌面/窗口 + 麦/声卡 → 录文件或推流。

    参数:
      output      输出路径: 本地文件 (.mp4) 或 rtmp:// 推流地址 (必填)
      video       视频源: 'desktop'(默认) / 'window:标题子串' / 'none'(无视频) / int(设备索引)
      audio       音频源: 'mic'(默认) / 'loopback'(声卡回环) / 'none'(无音频) / int(设备索引)
      bTranscode  True=转码(可叠处理, 进输出文件); False=转封装(零损耗, 处理仅显示不进文件)
      watermark   水印参数 dict 或 None; 如 {"centerX":.5,"centerY":.92,"width":.3,"height":.08,"alaph":.9}
                  第二项为图片路径 (见 enableWatermark); 传 tuple (params, image_path)
      adjust      画面调节 dict 或 None; 如 {"brightness":1.2,"saturation":1.1}
      duration    录制时长(秒); 0=不限(调用方自行 stop, 或 Ctrl+C)
      verbose     True 打印进度到 stdout

    返回 dict:
      {ok:bool, output:str, videoSrc:str, audioSrc:str, duration:float, errors:list}
      ok=False 表示启动失败 (设备/输出问题), errors 记原因。
    """
    result = {'ok': False, 'output': output, 'videoSrc': '', 'audioSrc': '',
              'duration': 0.0, 'errors': []}
    errs = result['errors']
    log = make_logger(verbose)

    # 验证输出
    if not output:
        errs.append('output 为空')
        return result
    if not _is_rtmp(output):
        outdir = os.path.dirname(os.path.abspath(output))
        os.makedirs(outdir, exist_ok=True)

    # 选设备
    vn = pick_video_device(video, errs)
    an = pick_audio_device(audio, errs)
    if vn is None and an is None:
        errs.append('video 和 audio 都为 none, 无可采集源')
    if errs:
        return result

    # 记录实际源名 (供返回)
    result['videoSrc'] = str(video)
    result['audioSrc'] = str(audio)

    p = None
    mux = None
    try:
        p = Player.ISourcePlayer()
        if vn:
            p.setVideoSource(vn)
        if an:
            p.setAudioSource(an)

        # 叠处理 (仅 bTranscode=True 时进输出文件; 无视频源时无 SurfaceRender)
        if vn and bTranscode:
            sr = p.getSurfaceRender()
            if watermark:
                if isinstance(watermark, tuple) and len(watermark) == 2:
                    sr.enableWatermark(watermark[0], watermark[1])
                elif isinstance(watermark, dict):
                    sr.enableWatermark(watermark)
            if adjust:
                sr.enableBasicAdjust(adjust)

        p.open()

        # 等待源就绪 (回调 + Event)
        import threading
        ready = threading.Event()
        p.onReady(lambda: ready.set())
        p.onStateChange(lambda pre, s: ready.set() if s in (PlayerState.ready, PlayerState.playing) else None)
        if not ready.wait(timeout=10):
            errs.append(f'源未就绪 (state={p.state})')
            return result
        log(f'采集已启动: video={video} audio={audio}')

        # 输出
        mux = p.getMuxer()
        mux.setMuxerType(MuxerType.ffmpeg)
        mux.setHardEncode(False)
        mux.setVideoCodec(VCodecId.h264)
        mux.setAudioCodec(ACodecId.aac)
        if not mux.open(output):
            errs.append(f'muxer.open 失败: {output}')
            return result
        kind = '推流' if _is_rtmp(output) else '录文件'
        log(f'{kind} → {output}')

        # 等待
        if duration > 0:
            target_ms = duration * 1000
            recorded_ms = [0]

            def on_prog(prog):
                recorded_ms[0] = prog.currentTimeMs
            mux.onProgress(on_prog)
            log(f'录制 {duration}s ...')
            t0 = time.time()
            while recorded_ms[0] < target_ms and time.time() - t0 < duration + 10:
                time.sleep(0.2)
            result['duration'] = recorded_ms[0] / 1000.0
            log(f'录制完成 (实际 {result["duration"]:.1f}s)')
            mux.close()
            p.close()
        else:
            log('持续采集中 (duration=0, 需调用方 stop 或 Ctrl+C)')
            # 不退出, 返回 player/muxer 供外部控制
            result['_player'] = p
            result['_muxer'] = mux

        result['ok'] = True
        return result
    except Exception as e:
        errs.append(f'capture: {e}')
        return result


def stop(result):
    """停止 run() 返回的采集会话 (duration=0 时需要手动停止)。
    传入 run() 返回的 dict, 会 close muxer + close player。"""
    mux = result.pop('_muxer', None)
    p = result.pop('_player', None)
    try:
        if mux:
            mux.close()
    except Exception:
        pass
    try:
        if p:
            p.close()
    except Exception:
        pass
