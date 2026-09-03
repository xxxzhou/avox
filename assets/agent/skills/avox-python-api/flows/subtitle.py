# flows/subtitle.py — 媒体 → 抽音频 → STT 字幕 → 中文字幕 (端到端组合功能)
#
# run(media, outdir, ...) 一步到位, 返回结构化 dict (AI 直接 run_code 调用)。
# 三步流水线 (TranscodeRecorder 批量解码, 远快于实时):
#   ① 提取音频    createRecorder(transcode) + setVideoCodec(none) → 仅音频 .mp4
#   ② STT → 字幕  createAudioStt(sherpa, offline) + AudioTap 喂 recognize → .srt
#   ③ 翻译 → 中文 createTranslator(http 优先 / onnx 回退) 逐条 translate → .zh.srt
# 缺模型/凭证时对应步跳过并在 errors 记原因, 不抛异常 (AI 拿 dict 判断)。
#
# 注: 音频录制输出必须用 .mp4 (audio-only 转 .aac/.m4a 在编码处崩溃, 录音器待修)。
#     STT 默认 offline (SenseVoice 多语种, 支持日/中/英/韩); streaming 是 zh-en 双语,
#     不支持日语等——日语素材勿切 streaming。

import os
import time
import tempfile
import threading

from avox import Muxer
from avox._core import RecorderState, VCodecId, AudioSttType, RecognizerType, TranslatorType, Language
import AvoxWrapper as _pw
from flows.common import fmt_time_ms, write_srt, parse_srt, make_logger


# ─── Step 1: 提取音频 (TranscodeRecorder + setVideoCodec(none)) ──

def _extract_audio(src, out_audio, errors, log):
    """→ bool 成功。仅音频 → .mp4"""
    rec = None
    try:
        rec = Muxer.createRecorder(bTranscode=True)
        if not rec:
            errors.append('step1: createRecorder 返回 None')
            return False
        rec.setVideoCodec(VCodecId.none)
        done = threading.Event()
        rec.onComplete(lambda: done.set())
        if not rec.open(src, out_audio):
            errors.append('step1: open 失败')
            return False
        if not done.wait(timeout=600):
            errors.append(f'step1: 超时 (state={rec.state})')
            return False
        ok = (rec.state == RecorderState.completed and os.path.isfile(out_audio)
              and os.path.getsize(out_audio) > 0)
        if not ok:
            errors.append(f'step1: 输出无效 (state={rec.state})')
        return ok
    except Exception as e:
        errors.append(f'step1: {e}')
        return False
    finally:
        if rec:
            try:
                rec.destroy()
            except Exception:
                pass


# ─── Step 2: STT → 字幕 (TranscodeRecorder + AudioTap, 批量快) ─

def _stt_to_srt(out_audio, out_srt, stt_mode, errors, log):
    """→ list[(startMs,endMs,text)] 或 None"""
    stt = None
    rec = None
    try:
        stt = _pw.createAudioStt(AudioSttType.sherpa)
        if not stt:
            errors.append('step2: createAudioStt 返回 None (插件未注册?)')
            return None
        recog = (RecognizerType.streaming if stt_mode == 'streaming'
                 else RecognizerType.offline)
        stt.setRecognizerType(recog)
        stt.setModelLevel(_pw.ModelLevel_base)

        segs = []

        class SttOb(_pw.IAudioSttOb):
            def __init__(self):
                _pw.IAudioSttOb.__init__(self)

            def onResult(self, result, text):
                if text:
                    segs.append((result.startPts, result.endPts, text))

            def onPartialResult(self, text):
                pass

            def onEndpoint(self):
                pass

        ob = SttOb()
        ob.__disown__()
        _pw.addAudioSttOb(stt, ob)

        # 开始识别任务 (内部加载模型); loading()==True 表示就绪。模型缺失则跳过
        stt.start()
        t0 = time.time()
        while not stt.loading() and time.time() - t0 < 60:
            time.sleep(0.3)
        if not stt.loading():
            errors.append('step2: STT 模型未就绪 (用 fetch_assets 取模型)')
            return None

        # TranscodeRecorder 解码 out_audio (批量, 无 PTS 节流), AudioTap 同步喂 recognize
        rec = Muxer.createRecorder(bTranscode=True)
        rec.setVideoCodec(VCodecId.none)
        ar = rec.getAudioRender()
        if not ar:
            errors.append('step2: getAudioRender 返回 None')
            return None
        state = {'desc': False}

        def on_desc(desc):
            try:
                stt.setAudioDesc(desc)
                state['desc'] = True
            except Exception as ex:
                errors.append(f'step2: setAudioDesc {ex}')

        def on_frame(raw, pts):
            if state['desc']:
                stt.recognize(raw, pts)

        ar.onAudioDesc(on_desc)
        ar.onFrame(on_frame)
        ar.openTap(None, 100)

        # open 必须给可写 outputFile (.mp4 容器! .aac/.m4a 会崩), 只取 tap → 临时文件
        tmp = tempfile.NamedTemporaryFile(suffix='.mp4', delete=False)
        tmp.close()
        rec_done = threading.Event()
        rec.onComplete(lambda: rec_done.set())
        if not rec.open(out_audio, tmp.name):
            errors.append('step2: open 失败')
            return None
        rec_done.wait(timeout=600)
        try:
            ar.closeTap()
        except Exception:
            pass

        # 末尾结果可能晚于最后一帧 (STT 工作线程异步排空), 轮询结果数稳定再 unload
        prev = -1
        stable = 0
        t0 = time.time()
        while time.time() - t0 < 1800:
            cur = len(segs)
            if cur == prev:
                stable += 1
            else:
                stable = 0
                prev = cur
            if stable >= 50:
                break
            time.sleep(0.2)
        try:
            stt.stop()
        except Exception:
            pass
        try:
            os.unlink(tmp.name)
        except Exception:
            pass

        if not segs:
            errors.append('step2: 无识别结果 (音频无人声/模型问题?)')
            return None
        write_srt(out_srt, segs)
        return segs
    except Exception as e:
        errors.append(f'step2: {e}')
        return None
    finally:
        try:
            if stt:
                stt.stop()
        except Exception:
            pass
        if rec:
            try:
                rec.destroy()
            except Exception:
                pass


# ─── Step 3: 翻译 → 中文字幕 ─────────────────────────────

def _translate(out_srt, out_zh, translator_mode, errors, log):
    """→ int 翻译条数, 0=失败"""
    try:
        segs = parse_srt(out_srt)
        if not segs:
            errors.append('step3: 解析字幕无条目')
            return 0

        # 默认腾讯云(http), 失败回退本地(onnx); translator_mode 可指定
        if translator_mode == 'onnx':
            order = [(TranslatorType.onnx, '本地(onnx)')]
        elif translator_mode == 'http':
            order = [(TranslatorType.http, '腾讯云(http)')]
        else:
            order = [(TranslatorType.http, '腾讯云(http)'),
                     (TranslatorType.onnx, '本地(onnx)')]

        tr = None
        backend = None
        for tt, name in order:
            cand = _pw.createTranslator(tt)
            if not cand:
                continue
            cand.setTargetLanguage(Language.zh)
            if cand.open():
                tr = cand
                backend = name
                break
            err = ''
            try:
                err = cand.getLastError() or ''
            except Exception:
                pass
            errors.append(f'step3: translator({name}) load 失败 {err}')
            try:
                cand.close()
            except Exception:
                pass
        if not tr:
            errors.append('step3: 无可用翻译后端 (腾讯云凭证/本地模型缺失)')
            return 0

        out = []
        for st, et, txt in segs:
            try:
                zh = tr.translate(txt)
            except Exception:
                zh = ''
            out.append((st, et, zh or txt))
        write_srt(out_zh, out)
        return len(out)
    except Exception as e:
        errors.append(f'step3: {e}')
        return 0


# ─── 入口 ────────────────────────────────────────────────

def run(media, outdir=None, stt='offline', translator='auto', verbose=True):
    """端到端: 媒体 → 抽音频(.mp4) → STT(.srt) → 翻译(.zh.srt)。

    参数:
      media      输入媒体文件路径 (必填)
      outdir     输出目录 (默认与源同目录)
      stt        'offline'(默认, SenseVoice 多语种) / 'streaming'(zh-en, 不支持日语)
      translator 'auto'(默认, http 优先 onnx 回退) / 'http' / 'onnx'
      verbose    True 打印进度到 stdout (run_code 可见)

    返回 dict:
      {ok:bool, audio:str, srt:str, zh:str, segs:int, translated:int, errors:list}
      缺模型/凭证 → 对应产出为空、errors 记原因, ok 反映是否三步全成。
    """
    src = os.path.abspath(media)
    result = {'ok': False, 'audio': '', 'srt': '', 'zh': '',
              'segs': 0, 'translated': 0, 'errors': []}
    errs = result['errors']
    log = make_logger(verbose)

    if not os.path.isfile(src):
        errs.append(f'文件不存在: {src}')
        return result

    outdir = outdir or os.path.dirname(src) or '.'
    os.makedirs(outdir, exist_ok=True)
    base = os.path.splitext(os.path.basename(src))[0]
    out_audio = os.path.join(outdir, base + '_audio.mp4')
    out_srt = os.path.join(outdir, base + '.srt')
    out_zh = os.path.join(outdir, base + '.zh.srt')

    # Step 1
    log(f'[1/3] 提取音频 → {out_audio}')
    if not _extract_audio(src, out_audio, errs, log):
        log('  ✗ Step1 失败, 后续跳过')
        return result
    result['audio'] = out_audio
    log('  ✓ 完成')

    # Step 2
    log(f'[2/3] STT 识别 ({stt}) → {out_srt}')
    segs = _stt_to_srt(out_audio, out_srt, stt, errs, log)
    if not segs:
        log('  ✗ Step2 失败, 翻译跳过')
        return result
    result['srt'] = out_srt
    result['segs'] = len(segs)
    log(f'  ✓ {len(segs)} 段')

    # Step 3
    log(f'[3/3] 翻译 → 中文 ({translator}) → {out_zh}')
    n = _translate(out_srt, out_zh, translator, errs, log)
    if n:
        result['zh'] = out_zh
        result['translated'] = n
        log(f'  ✓ {n} 条')
    else:
        log('  ✗ Step3 失败 (仍保留原语言 .srt)')

    result['ok'] = bool(result['audio'] and result['srt'] and result['zh'])
    return result
