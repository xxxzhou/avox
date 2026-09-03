# flows/vlm.py — 取帧/音频 → 喂 AI 大模型 (VLM 画面描述 + STT 语音转文字)
#
# 入口: run(src, out_dir, source='media', modes=('vlm','stt'), ...) → 结构化 dict。
# 本 flow 的特点: 把 avox 取出来的帧/音频直接喂给外部 AI 大模型, 组合现有 flow + VLM:
#   vlm : 复用 extract 取帧 (三种宿主) → 缩放 → base64 → 调 OpenAI 兼容 VLM → 中文画面描述
#   stt : media 文件 → 复用 subtitle 的 STT pipeline (sherpa) → 文字 (跳过翻译)
# VLM 默认连本地 LM Studio (http://localhost:1234, 模型 qwen3-vl-4b);
# lm_url/lm_model 可改指向任何 OpenAI 兼容服务 (vLLM / Ollama / 远端 API)。
# 缺 VLM server / STT 模型时对应项跳过并在 errors 记原因, 不抛异常 (AI 拿 dict 判断)。
#
# 集显带图慢 (UHD770 实测 Qwen3-VL-4B ~26s/帧): frame_count 默认 1;
# 设 >1 时多帧合并到一个请求做"片段综合理解" (视觉 token 线性增多, 更慢)。
# STT 仅支持 source='media' 的本地文件 (subtitle 走文件批量解码); 设备/直播源 STT 跳过。

import os
import time
import json
import urllib.request

from avox import Image
from flows.common import make_logger
from flows import extract as _extract
from flows.subtitle import _extract_audio, _stt_to_srt


# ─── VLM: 帧 → base64 → 画面描述 ─────────────────────────

def _frame_to_b64(path, img_w):
    """读 PNG → 等比缩放到 img_w 宽 → JPEG base64 字符串。失败返回 None。"""
    buf = Image.loadImage(path)
    if buf is None:
        return None
    h = int(img_w * buf.height / buf.width) if buf.width else img_w
    small = Image.resize(buf, img_w, h) or buf   # resize 失败回退原图
    return small.to_base64(quality=85)


def _vlm_describe(frame_paths, prompt, lm_url, lm_model, img_w, max_tokens, errors, log):
    """多帧合并喂一个 VLM 请求 → (描述文字, 耗时s)。无图或调用失败返回 ('', elapsed)。

    OpenAI 兼容 chat/completions: content 为 [text, image_url, ...]。
    多帧时模型一次看到全部图, 适合"这段画面综合在做什么"。"""
    content = [{"type": "text", "text": prompt}]
    for p in frame_paths:
        b64 = _frame_to_b64(p, img_w)
        if b64:
            content.append({"type": "image_url",
                            "image_url": {"url": f"data:image/jpeg;base64,{b64}"}})
    if len(content) == 1:
        errors.append('vlm: 取到 0 帧可喂 (检查 extract 取帧是否成功)')
        return '', 0.0
    body = json.dumps({"model": lm_model, "max_tokens": max_tokens, "temperature": 0.3,
                       "messages": [{"role": "user", "content": content}]}).encode()
    t0 = time.time()
    try:
        resp = urllib.request.urlopen(urllib.request.Request(
            lm_url, data=body, headers={"Content-Type": "application/json"}), timeout=180)
        text = json.loads(resp.read())["choices"][0]["message"]["content"]
        return text, time.time() - t0
    except Exception as e:
        errors.append(f'vlm 调用失败 (确认 {lm_url} 可达、模型名 {lm_model} 对): {e}')
        return '', time.time() - t0


# ─── STT: 音频 → 文字 (复用 subtitle pipeline, 跳过翻译) ──

def _stt_text(src, out_dir, stt_mode, errors, log):
    """media 文件 → 抽音频 → sherpa STT → (文字, srt路径, 段数)。失败返回 ('', '', 0)。"""
    out_audio = os.path.join(out_dir, 'vlm_audio.mp4')
    out_srt = os.path.join(out_dir, 'vlm.srt')
    if not _extract_audio(src, out_audio, errors, log):
        return '', '', 0
    segs = _stt_to_srt(out_audio, out_srt, stt_mode, errors, log)
    if not segs:
        return '', '', 0
    text = '\n'.join(txt for _, _, txt in segs)
    return text, out_srt, len(segs)


# ─── 入口 ────────────────────────────────────────────────

def run(src=None, out_dir='.', source='media', modes=('vlm', 'stt'),
        lm_url='http://localhost:1234/v1/chat/completions', lm_model='qwen3-vl-4b',
        prompt='用中文描述画面里发生了什么。区分主体动作和画面文字/UI信息,简洁。',
        frame_count=1, frame_interval=1.0, img_w=768, max_tokens=200,
        stt='offline', verbose=True):
    """取帧/音频 → 喂 AI 大模型 (VLM 画面描述 + STT 语音转文字)。

    参数:
      src       媒体 URL/本地路径 (media、recorder 必填; source 模式忽略)
      out_dir   输出目录 (帧 PNG + srt + 描述文本)
      source    宿主: 'media'(默认)/'source'/'recorder' (透传给 extract)
      modes     启用项 ('vlm','stt') 子集; 默认全开。也接受单字符串
      lm_url    OpenAI 兼容 chat/completions 地址 (默认本地 LM Studio)
      lm_model  模型名 (默认 qwen3-vl-4b; 改成你的 /v1/models 返回的 id)
      prompt    喂给 VLM 的画面描述指令
      frame_count  取几帧喂 VLM, 默认 1; >1 多帧合并综合理解 (视觉 token 增多, 更慢)
      frame_interval  取帧间隔(秒), 默认 1.0
      img_w     喂 VLM 前缩放到的宽度(像素), 默认 768 (集显越小越快, 太小看不清文字)
      max_tokens  VLM 生成上限, 默认 200
      stt       STT 模式 'offline'(默认, 多语种)/'streaming'(zh-en)
      verbose   打印进度到 stdout

    返回 dict:
      {ok, source,
       vlm: {ok, description, frames, elapsed, errors},   # elapsed=VLM 调用耗时(s)
       stt: {ok, text, srt, segs, errors},
       errors:[...]}                                       # 汇总所有问题
    ok=True 表示至少一项成功; 各项成败看对应子 dict。
    STT 仅 source='media' 的本地文件支持; 其它源 STT 跳过并记 errors。
    """
    if isinstance(modes, str):
        modes = (modes,)
    result = {'ok': False, 'source': source,
              'vlm': {'ok': False, 'description': '', 'frames': 0, 'elapsed': 0.0, 'errors': []},
              'stt': {'ok': False, 'text': '', 'srt': '', 'segs': 0, 'errors': []},
              'errors': []}
    errs = result['errors']
    log = make_logger(verbose)
    os.makedirs(out_dir, exist_ok=True)

    # ── VLM: 复用 extract 取帧 → base64 → 描述 ──
    if 'vlm' in modes:
        log(f'[vlm] 取 {frame_count} 帧 (source={source}) ...')
        ex = _extract.run(src=src, out_dir=out_dir, source=source,
                          interval=frame_interval, count=frame_count,
                          modes=('screenshot',), verbose=False)
        frames = ex.get('screenshot', {}).get('frames', [])
        result['vlm']['frames'] = len(frames)
        if not frames:
            result['vlm']['errors'] = ex.get('screenshot', {}).get('errors') or ['vlm: extract 未取到帧']
            errs.extend(result['vlm']['errors'])
            log('  ✗ 取帧失败')
        else:
            desc, elapsed = _vlm_describe(frames, prompt, lm_url, lm_model, img_w,
                                          max_tokens, result['vlm']['errors'], log)
            result['vlm']['elapsed'] = elapsed
            if desc:
                result['vlm']['description'] = desc
                result['vlm']['ok'] = True
                try:
                    with open(os.path.join(out_dir, 'vlm_description.txt'), 'w', encoding='utf-8') as f:
                        f.write(desc)
                except Exception:
                    pass
                log(f'  ✓ {elapsed:.1f}s: {desc[:80]}')
            else:
                errs.extend(result['vlm']['errors'])
                log(f'  ✗ VLM 调用失败 ({elapsed:.1f}s)')

    # ── STT: media 文件 → 文字 (复用 subtitle) ──
    if 'stt' in modes:
        if source == 'media' and src and os.path.isfile(src):
            log(f'[stt] 音频 → STT ({stt}) ...')
            text, srt, n = _stt_text(src, out_dir, stt, result['stt']['errors'], log)
            if text:
                result['stt']['text'] = text
                result['stt']['srt'] = srt
                result['stt']['segs'] = n
                result['stt']['ok'] = True
                log(f'  ✓ {n} 段: {text[:80]}')
            else:
                errs.extend(result['stt']['errors'])
                log('  ✗ STT 无结果')
        else:
            msg = f'stt: 仅 source=media 的本地文件支持, 跳过 (source={source})'
            result['stt']['errors'].append(msg)
            errs.append(msg)
            log(f'  ⊘ {msg}')

    result['ok'] = result['vlm']['ok'] or result['stt']['ok']
    log(f"=== vlm 完成: ok={result['ok']} "
        f"vlm={result['vlm']['ok']} stt={result['stt']['ok']} ===")
    return result
