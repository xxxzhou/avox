# flows/loops.py — 截图→识别→动作 (RPA 重试循环; 采集→录制见 flows/capture.py)
#
# 通用入口 vision_loop + 4 个便捷别名:
#   desktop_image_loop(tmpl)   抓桌面 → 模板匹配 → 点击
#   desktop_text_loop(text)    抓桌面 → OCR文字匹配 → 点击
#   window_image_loop(tmpl,w)  抓窗口 → 模板匹配 → 点击
#   window_text_loop(text,w)   抓窗口 → OCR文字匹配 → 点击
#
# 桌面态管理 (show_desktop=True):
#   进入前记录前台窗口 → showDesktop() 最小化所有窗口 →
#   操作完毕后 undoDesktop() + activateHwnd 还原。找不到目标也还原, 避免桌面卡在最小化。

import time

from avox import Image, Input, Vision
from avox._core import MouseButton, TemplateMatchMethod, MatchOrderBy
from flows.common import make_logger


# ─── 截图目标 ─────────────────────────────────────────────

def _setup_capture(window=None, show_desktop=False):
    """创建 ScreenCapture 并设定目标。
    window=None 抓桌面第0屏, 否则按标题子串抓窗口。
    返回 (cap, target_desc, before_hwnd)。"""
    cap = Input.createScreenCapture()
    before = None
    if window:
        cap.setWindow(window)
        return cap, f'窗口 "{window}"', before
    # 桌面模式
    if show_desktop:
        before = Vision.getActiveWindow()
    cap.setScreen(0, showDesktop=show_desktop)
    return cap, '桌面第0屏', before


def _restore_desktop(cap, before, log):
    """还原 showDesktop 最小化的窗口。"""
    try:
        cap.undoDesktop()
        time.sleep(0.3)
    except Exception:
        pass
    if before:
        try:
            cap.activateHwnd(before)
        except Exception:
            pass


# ─── 模板匹配 ─────────────────────────────────────────────

def _create_matcher(tmpl_path, threshold, errors):
    """创建并配置模板匹配器, 返回 matcher 或 None。"""
    matcher = Vision.createTemplateMatcher()
    if matcher is None:
        errors.append('avox_opencv 未启用, 模板匹配不可用')
        return None
    matcher.setMethod(TemplateMatchMethod.ccoeffNormed)
    matcher.setOrderBy(MatchOrderBy.score)
    if matcher.addTemplatePath(tmpl_path, threshold) < 0:
        errors.append(f'模板加载失败: {tmpl_path}')
        return None
    return matcher


def _find_image(matcher, buf, cap, errors):
    """模板匹配: 返回 {matchType, matchInfo, screenPos} 或 None。"""
    if matcher.match(buf) <= 0:
        return None
    r = matcher.getMatch(0)
    c = matcher.matchCenter(r)
    sp = cap.toScreen(c.x, c.y)
    return {
        'matchType': 'image',
        'matchInfo': {'x': r.x, 'y': r.y, 'w': r.w, 'h': r.h, 'score': r.score,
                      'centerX': c.x, 'centerY': c.y},
        'screenPos': [sp.x, sp.y]
    }


# ─── OCR 文字匹配 ─────────────────────────────────────────

def _create_ocr(threshold, errors):
    """创建并加载 OCR 识别器, 返回 ocr 或 None。"""
    ocr = Vision.createTextRecognizer()
    if ocr is None:
        errors.append('avox_ocr 未启用, OCR 不可用')
        return None
    ocr.setThreshold(threshold)
    return ocr


def _find_text(ocr, buf, text, cap, errors):
    """OCR 文字匹配: 返回 {matchType, matchInfo, screenPos} 或 None。
    多个命中时优先选文本最短的 (避免长文本如终端代码行中包含目标子串)。"""
    ocr.recognize(buf)
    target = text.strip()
    best = None
    for i in range(ocr.getMatchCount()):
        t, r = ocr.getMatch(i)
        if t is None or r is None:
            continue
        if target and target in t:
            c = ocr.ocrCenter(r)
            sp = cap.toScreen(c.x, c.y)
            hit = {
                'matchType': 'text',
                'matchInfo': {'text': t, 'x': r.x, 'y': r.y, 'w': r.w, 'h': r.h,
                              'score': r.score, 'centerX': c.x, 'centerY': c.y},
                'screenPos': [sp.x, sp.y]
            }
            if best is None or len(t) < len(best['matchInfo']['text']):
                best = hit
    return best


# ─── 通用循环 ─────────────────────────────────────────────

def _action_loop(find_fn, cap, target_desc, inp, before, window_title,
                 max_retries, interval, double_click, show_desktop, log):
    """通用 RPA 循环: 反复截图 → find_fn(buf, cap) → 命中则点击。
    find_fn 返回 {matchType, matchInfo, screenPos} 或 None。
    window_title 非空时, 点击前 activateWindow 把窗口拉到前台。"""
    result = {'ok': False, 'found': False, 'clicked': False,
              'screenPos': None, 'matchType': None, 'matchInfo': None,
              'retryCount': 0, 'errors': []}

    log(f'抓 {target_desc}, 开始循环匹配 (最多 {max_retries} 次)')

    # show_desktop 模式: setScreen(showDesktop=True) 只做一次, 后续 refresh 用 showDesktop=False
    if show_desktop:
        cap.setScreen(0, showDesktop=False)

    try:
        for attempt in range(max_retries):
            result['retryCount'] = attempt + 1
            cap.refresh()
            native_buf = cap.getBuffer()
            if not native_buf:
                log(f'  [{attempt+1}/{max_retries}] 截图为空, 重试')
                time.sleep(interval)
                continue
            buf = Image.IImageBuffer(native_buf)

            hit = find_fn(buf, cap)
            if hit:
                result['found'] = True
                result['matchType'] = hit['matchType']
                result['matchInfo'] = hit['matchInfo']
                result['screenPos'] = hit['screenPos']
                sx, sy = hit['screenPos']
                info = hit['matchInfo']
                if hit['matchType'] == 'image':
                    log(f'  ✓ 模板命中 @ 缓冲({info["centerX"]},{info["centerY"]}) → 屏幕({sx},{sy}), score={info["score"]:.3f}')
                else:
                    log(f'  ✓ 文字命中 "{info["text"]}" @ 缓冲({info["centerX"]},{info["centerY"]}) → 屏幕({sx},{sy}), score={info["score"]:.3f}')
                # 点击前把窗口拉到前台
                if window_title:
                    try:
                        cap.activateWindow(window_title)
                        time.sleep(0.3)
                    except Exception:
                        pass
                if double_click:
                    inp.doubleClick(sx, sy)
                else:
                    inp.click(sx, sy, MouseButton.left)
                result['clicked'] = True
                result['ok'] = True
                log(f'  ✓ 已{"双击" if double_click else "单击"}')
                return result

            log(f'  [{attempt+1}/{max_retries}] 未命中, {interval}s 后重试')
            time.sleep(interval)

        log(f'  ✗ {max_retries} 次重试均未命中')
        result['ok'] = True
        return result
    finally:
        if show_desktop:
            _restore_desktop(cap, before, log)
            log('已还原桌面状态')


# ─── 通用入口 ─────────────────────────────────────────────

def vision_loop(target, mode='image', window=None, threshold=None,
                max_retries=10, interval=1.0, double_click=False,
                show_desktop=False, verbose=True):
    """截图 → 识别 → 点击命中中心 (重试循环)。

    参数:
      target       模板图片路径 (mode='image') 或要匹配的文字 (mode='text')
      mode         'image'(模板匹配) / 'text'(OCR文字匹配)
      window       None=抓桌面 / 窗口标题子串=抓指定窗口
      threshold    匹配阈值 (image 默认 0.8, text 默认 0.3)
      max_retries  最大重试次数
      interval     每次重试间隔(秒)
      double_click True=双击, False=单击
      show_desktop True=先显示桌面(最小化所有窗口), 操作后自动还原 (仅桌面模式)
      verbose      True 打印进度到 stdout

    返回 dict:
      {ok:bool, found:bool, clicked:bool, screenPos:[x,y]|None,
       matchType:'image'|'text'|None, matchInfo:dict|None, retryCount:int, errors:list}
    """
    result = {'ok': False, 'found': False, 'clicked': False,
              'screenPos': None, 'matchType': None, 'matchInfo': None,
              'retryCount': 0, 'errors': []}
    errs = result['errors']
    log = make_logger(verbose)

    if threshold is None:
        threshold = 0.8 if mode == 'image' else 0.3

    cap, desc, before = _setup_capture(window=window, show_desktop=show_desktop and not window)

    # 创建识别器
    if mode == 'image':
        matcher = _create_matcher(target, threshold, errs)
        if not matcher:
            if show_desktop and not window:
                _restore_desktop(cap, before, log)
            return result
        find_fn = lambda buf, cap: _find_image(matcher, buf, cap, errs)
    else:
        ocr = _create_ocr(threshold, errs)
        if not ocr:
            if show_desktop and not window:
                _restore_desktop(cap, before, log)
            return result
        find_fn = lambda buf, cap: _find_text(ocr, buf, target, cap, errs)

    inp = Input.createInputController()
    inp.setMoveDurationMs(80)

    return _action_loop(find_fn, cap, desc, inp, before, window,
                        max_retries, interval, double_click,
                        show_desktop and not window, log)


# ─── 便捷别名 ─────────────────────────────────────────────

def desktop_image_loop(tmpl_path, threshold=0.8, max_retries=10, interval=1.0,
                       double_click=False, show_desktop=False, verbose=True):
    """抓桌面 → 模板匹配 → 点击命中中心。"""
    return vision_loop(tmpl_path, mode='image', window=None, threshold=threshold,
                       max_retries=max_retries, interval=interval,
                       double_click=double_click, show_desktop=show_desktop,
                       verbose=verbose)


def desktop_text_loop(text, threshold=0.3, max_retries=10, interval=1.0,
                      double_click=False, show_desktop=False, verbose=True):
    """抓桌面 → OCR文字匹配 → 点击命中中心。"""
    return vision_loop(text, mode='text', window=None, threshold=threshold,
                       max_retries=max_retries, interval=interval,
                       double_click=double_click, show_desktop=show_desktop,
                       verbose=verbose)


def window_image_loop(tmpl_path, window, threshold=0.8, max_retries=10, interval=1.0,
                      double_click=False, verbose=True):
    """抓指定窗口 → 模板匹配 → 点击命中中心。"""
    return vision_loop(tmpl_path, mode='image', window=window, threshold=threshold,
                       max_retries=max_retries, interval=interval,
                       double_click=double_click, verbose=verbose)


def window_text_loop(text, window, threshold=0.3, max_retries=10, interval=1.0,
                     double_click=False, verbose=True):
    """抓指定窗口 → OCR文字匹配 → 点击命中中心。"""
    return vision_loop(text, mode='text', window=window, threshold=threshold,
                       max_retries=max_retries, interval=interval,
                       double_click=double_click, verbose=verbose)
