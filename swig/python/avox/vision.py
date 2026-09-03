# avox SDK Python 高层封装 — Vision 模块
# ITemplateMatcher, ITextRecognizer + 窗口辅助
# (从 input.py 抽出; input.py 仍 re-export 保向后兼容)

import AvoxWrapper as _pw
from avox._core import TemplateMatchMethod, MatchOrderBy, FeatureMethod, ColorSpace
from dataclasses import dataclass
import ctypes

# ─── ITemplateMatcher ───────────────────────────────────


class ITemplateMatcher:
    """模板/图标匹配封装 (AvoxVision.h)。

    在场景图中定位图标/模板位置, 供 Agent 点击或质检。拉模式: 配置 → addTemplate →
    match(scene) → getMatch 循环取结果。method 见 TemplateMatchMethod, orderBy 见 MatchOrderBy。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createTemplateMatcher()

    # ── 配置 ──

    def setMethod(self, method):
        """匹配方法 (TemplateMatchMethod), 默认 ccoeffNormed。"""
        return self._native.setMethod(method)

    def setOrderBy(self, orderBy):
        """结果排序 (MatchOrderBy), 默认 horizontal。"""
        return self._native.setOrderBy(orderBy)

    def setGreenMask(self, enable):
        """绿色掩码: 模板中纯绿区域当透明不参与匹配 (仅 sqdiffNormed/ccorrNormed)。"""
        return self._native.setGreenMask(enable)

    def setGrayscale(self, enable):
        """灰度匹配: scene 和模板都转单通道灰度再匹配, 对光照/颜色变化更鲁棒。
        适合小图标 (如地图花图标); grayscale 模式下 greenMask 被忽略。"""
        return self._native.setGrayscale(enable)

    def setNmsIoU(self, iou):
        """NMS 去重 IoU 阈值, 默认 0.2; 设 0 关闭。"""
        return self._native.setNmsIoU(iou)

    def setRoi(self, x, y, w, h):
        """感兴趣区域, 默认整图。"""
        return self._native.setRoi(x, y, w, h)

    def clearRoi(self):
        """清除感兴趣区域, 恢复整图匹配。"""
        return self._native.clearRoi()

    # ── 模板 ──

    def addTemplate(self, tmpl, threshold):
        """加模板 (IImageBuffer), threshold 命中阈值 [0,1] 推荐 0.7~0.8; 返回模板下标, 失败 -1。"""
        native = tmpl._native if hasattr(tmpl, '_native') else tmpl
        return self._native.addTemplate(native, threshold)

    def addTemplatePath(self, path, threshold):
        """从图片文件路径加模板。threshold 命中阈值 [0,1] 推荐 0.7~0.8。"""
        return self._native.addTemplatePath(path, threshold)

    def clearTemplates(self):
        """清除所有已添加的模板。"""
        return self._native.clearTemplates()

    # ── 执行 ──

    def match(self, scene):
        """在 scene 中匹配所有模板, 返回过滤后命中数量。"""
        native = scene._native if hasattr(scene, '_native') else scene
        return self._native.match(native)

    # ── 结果 ──

    def getMatchCount(self):
        """获取命中数量。"""
        return self._native.getMatchCount()

    def getMatch(self, index):
        """取第 index 个命中 (支持负索引), 返回 _pw.MatchResult (.x/.y/.w/.h/.score/.templateIndex), 越界 None。"""
        out = _pw.MatchResult()
        if not self._native.getMatch(index, out):
            return None
        return out

    def getMatchTimeMs(self):
        """最近一次 match 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    def matchCenter(self, result):
        """取命中框中心 (供点击), 返回 _pw.vec2i (.x/.y)。"""
        return _pw.getMatchCenter(result)

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── ITextRecognizer ────────────────────────────────────


class ITextRecognizer:
    """OCR 文字识别封装 (AvoxVision.h, PP-OCRv6 det+rec)。

    拉模式: 配置 → recognize(scene) → getMatch/getText 循环取结果 (模型首次 recognize 时按需加载)。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createTextRecognizer()

    # ── 配置 ──

    def setThreshold(self, threshold):
        """检测阈值 [0,1], 默认 0.3。"""
        return self._native.setThreshold(threshold)

    def setRoi(self, x, y, w, h):
        """感兴趣区域, 默认整图。"""
        return self._native.setRoi(x, y, w, h)

    def clearRoi(self):
        """清除感兴趣区域, 恢复整图匹配。"""
        return self._native.clearRoi()

    def setUseGpu(self, enable):
        """GPU 推理, 默认 False (CPU)。"""
        return self._native.setUseGpu(enable)

    # ── 执行 ──

    def recognize(self, scene):
        """det+rec 全图识别, 返回文字框数。"""
        native = scene._native if hasattr(scene, '_native') else scene
        return self._native.recognize(native)

    # ── 结果 ──

    def getMatchCount(self):
        """获取识别结果数量。"""
        return self._native.getMatchCount()

    def getMatch(self, index):
        """取第 index 个识别文字 (支持负索引), 返回 (text, OcrResult) 或 None。
        text 为 UTF-8 str, OcrResult 含 .x/.y/.w/.h/.score; 越界返回 None。"""
        out = _pw.OcrResult()
        text = self._native.getMatch(index, out)
        if text is None:
            return None
        return (text, out)

    def getMatchTimeMs(self):
        """最近一次 recognize 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    def ocrCenter(self, result):
        """取文字框中心 (供点击), 返回 _pw.vec2i (.x/.y)。"""
        return _pw.getOcrCenter(result)

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── IFeatureMatcher ────────────────────────────────────


class IFeatureMatcher:
    """特征匹配封装 (AvoxVision.h, SIFT/ORB/AKAZE + BFMatcher + Lowe + RANSAC)。

    在场景图中定位参考图位置(旋转/尺度/光照鲁棒)。拉模式: 配置 → addReference →
    match(scene) → getMatch 循环取结果。method 见 FeatureMethod。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createFeatureMatcher()

    # ── 配置 ──

    def setMethod(self, method):
        """描述子方法 (FeatureMethod), 默认 sift。"""
        return self._native.setMethod(method)

    def setMaxFeatures(self, n):
        """检测器特征点上限, 默认 1000。"""
        return self._native.setMaxFeatures(n)

    def setRatioThreshold(self, ratio):
        """Lowe 比率测试阈值, 默认 0.75。"""
        return self._native.setRatioThreshold(ratio)

    def setMinInliers(self, n):
        """接受命中的最小 RANSAC 内点数, 默认 8。"""
        return self._native.setMinInliers(n)

    def setRoi(self, x, y, w, h):
        """scene 上感兴趣区域, 默认整图。"""
        return self._native.setRoi(x, y, w, h)

    def clearRoi(self):
        """清除感兴趣区域, 恢复整图。"""
        return self._native.clearRoi()

    # ── 参考图 ──

    def addReference(self, ref):
        """加参考图 (IImageBuffer), 返回下标, 失败 -1。"""
        native = ref._native if hasattr(ref, '_native') else ref
        return self._native.addReference(native)

    def addReferencePath(self, path):
        """从图片文件路径加参考图。"""
        return self._native.addReferencePath(path)

    def clearReferences(self):
        """清除所有参考图。"""
        return self._native.clearReferences()

    # ── 执行 ──

    def match(self, scene):
        """在 scene 中定位所有参考图, 返回命中数 (score 降序)。"""
        native = scene._native if hasattr(scene, '_native') else scene
        return self._native.match(native)

    # ── BGI 大地图 SIFT 定位 (train=预存底图切块, query=视口实时算) ──

    def loadTrainFeaturesPath(self, kpPath, descPath, imgW, imgH, blockRows, blockCols):
        """一次性加载预存底图特征并切块缓存 (BGI 格式: kpPath=.kp.bin, descPath=.mat.png)。
        imgW/imgH=底图像素, blockRows/blockCols=切块网格。重复调用替换缓存。失败返回 False (查 getLastError)。"""
        return self._native.loadTrainFeaturesPath(kpPath, descPath, imgW, imgH, blockRows, blockCols)

    def matchQueryLocal(self, query, roiX, roiY, roiW, roiH, expandCells):
        """query(视口) 局部定位: roi=底图坐标系预期搜索矩形, 取其 ±expandCells 格做 FLANN knnMatch;
        局部失败内部回退全图。结果经 getMatch(0): .x/.y=query 中心在底图坐标, .w/.h=0 (点哨兵), .score=inliers/good。
        返回 1=命中 / 0=未定位 (查 getLastError)。"""
        native = query._native if hasattr(query, '_native') else query
        return self._native.matchQueryLocal(native, roiX, roiY, roiW, roiH, expandCells)

    def matchQueryFull(self, query):
        """query(视口) 全图兜底定位 (BGI Match: FLANN 单最佳 + 距离阈值 + RANSAC)。输出约定同 matchQueryLocal。"""
        native = query._native if hasattr(query, '_native') else query
        return self._native.matchQueryFull(native)

    # ── 结果 ──

    def getMatchCount(self):
        """获取命中数量。"""
        return self._native.getMatchCount()

    def getMatch(self, index):
        """取第 index 个命中 (支持负索引), 返回 _pw.FeatureMatchResult (.x/.y/.w/.h/.refIndex/.score), 越界 None。"""
        out = _pw.FeatureMatchResult()
        if not self._native.getMatch(index, out):
            return None
        return out

    def getMatchTimeMs(self):
        """最近一次 match 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    def matchCenter(self, result):
        """取命中框中心 (供点击/定位), 返回 _pw.vec2i (.x/.y)。"""
        return _pw.getFeatureMatchCenter(result)

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── IColorDetector ─────────────────────────────────────


class IColorDetector:
    """颜色区域检测封装 (AvoxVision.h, cv::inRange + connectedComponents)。

    检测落在颜色范围内的连通区域。拉模式: 配置 → setRange → detect(scene) →
    getRegion 循环取。colorSpace 见 ColorSpace。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createColorDetector()

    # ── 配置 ──

    def setColorSpace(self, cs):
        """颜色空间 (ColorSpace), 默认 bgr (屏捕 bgra8→bgr)。"""
        return self._native.setColorSpace(cs)

    def setRange(self, c0Min, c1Min, c2Min, c0Max, c1Max, c2Max):
        """颜色范围 (三通道上下界, 顺序由 ColorSpace 决定)。"""
        return self._native.setRange(c0Min, c1Min, c2Min, c0Max, c1Max, c2Max)

    def setRoi(self, x, y, w, h):
        """感兴趣区域, 默认整图。"""
        return self._native.setRoi(x, y, w, h)

    def clearRoi(self):
        """清除感兴趣区域, 恢复整图。"""
        return self._native.clearRoi()

    def setMinArea(self, area):
        """丢弃小于此面积的区域, 默认 8。"""
        return self._native.setMinArea(area)

    def setMaxRegions(self, n):
        """返回上限 (按面积降序截断), 默认 100。"""
        return self._native.setMaxRegions(n)

    # ── 执行 ──

    def detect(self, scene):
        """检测颜色区域, 返回区域数。"""
        native = scene._native if hasattr(scene, '_native') else scene
        return self._native.detect(native)

    # ── 结果 ──

    def getRegionCount(self):
        """获取区域数量。"""
        return self._native.getRegionCount()

    def getRegion(self, index):
        """取第 index 个区域 (支持负索引), 返回 _pw.ColorRegion (.x/.y/.w/.h/.area/.score), 越界 None。"""
        out = _pw.ColorRegion()
        if not self._native.getRegion(index, out):
            return None
        return out

    def getMatchTimeMs(self):
        """最近一次 detect 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    def regionCenter(self, result):
        """取区域中心 (供点击), 返回 _pw.vec2i (.x/.y)。"""
        return _pw.getColorRegionCenter(result)

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── IOrientationDetector ───────────────────────────────


class IOrientationDetector:
    """方向检测封装 (AvoxVision.h, 极坐标展开 + Scharr 梯度)。

    在圆形 ROI 内估计主导方向 (角度, 如小地图角色朝向)。setCircle 设圆心/半径 →
    compute(scene) 返回角度 [0,360), 约定 0=上(北)顺时针; 失败返回 -1。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createOrientationDetector()

    # ── 配置 ──

    def setCircle(self, cx, cy, radius):
        """圆形分析区域: 圆心 (cx,cy) + 半径 (scene 坐标系)。"""
        return self._native.setCircle(cx, cy, radius)

    def setAngleRange(self, angleMin, angleMax):
        """角度搜索范围 [angleMin, angleMax), 度, 默认 [0,360)。"""
        return self._native.setAngleRange(angleMin, angleMax)

    def setSmooth(self, degrees):
        """圆周平滑窗口 (度), 默认 5。"""
        return self._native.setSmooth(degrees)

    # ── 执行 ──

    def compute(self, scene):
        """返回主导角度 [0,360) (0=上顺时针); 失败 -1。"""
        native = scene._native if hasattr(scene, '_native') else scene
        return self._native.compute(native)

    # ── 结果 ──

    def getLastScore(self):
        """峰值强度/置信度 [0,1]。"""
        return self._native.getLastScore()

    def getMatchTimeMs(self):
        """最近一次 compute 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── IMapMatcher ──────────────────────────────────────


class IMapMatcher:
    """地图定位封装 (AvoxVision.h, BGI 模板匹配方案: 朝向去旋转 + 粗匹配 + 精匹配)。

    在全地图中定位小地图位置, 返回地图坐标 + 置信度。
    对照 BGI SceneBaseMapByTemplateMatch + FastSqDiffMatcher + MiniMapPreprocessor。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createMapMatcher()

    # ── 配置 ──

    def setMapImage(self, mapImg):
        """设置全地图彩图 (粗匹配用; BGI *_color.webp)。"""
        native = mapImg._native if hasattr(mapImg, '_native') else mapImg
        return self._native.setMapImage(native)

    def setMapImageByPath(self, path):
        """从图片文件路径加载全地图彩图。"""
        return self._native.setMapImageByPath(path)

    def setFineMapImage(self, mapImg):
        """设置灰度地图图 (精匹配用; BGI *_gray.webp)。"""
        native = mapImg._native if hasattr(mapImg, '_native') else mapImg
        return self._native.setFineMapImage(native)

    def setFineMapImageByPath(self, path):
        """从图片文件路径加载灰度地图图。"""
        return self._native.setFineMapImageByPath(path)

    def setRoi(self, x, y, w, h):
        """感兴趣区域 (全地图上的搜索区域)。"""
        return self._native.setRoi(x, y, w, h)

    def clearRoi(self):
        """清除感兴趣区域, 恢复全图搜索。"""
        return self._native.clearRoi()

    def setMinScore(self, score):
        """最低置信度, 默认 0.95。"""
        return self._native.setMinScore(score)

    def setCoarseSize(self, size):
        """粗匹配时小地图缩放尺寸, 默认 52。"""
        return self._native.setCoarseSize(size)

    def setExactSize(self, size):
        """精匹配时小地图尺寸, 默认 260。"""
        return self._native.setExactSize(size)

    def setSearchRadius(self, radius):
        """局部搜索半径 (精匹配, fineMap 像素, 有 prev_position 时), 默认 50。"""
        return self._native.setSearchRadius(radius)

    def setRoughSearchRadius(self, radius):
        """粗匹配局部搜索半径 (coarseMap 像素), 默认 0=全图搜索。
        配合 setPrevPosition 自动限定粗匹配 ROI 范围。"""
        return self._native.setRoughSearchRadius(radius)

    def setMask(self, mask):
        """外部二值掩码 (单通道, 255=参与/0=排除); 未设=全参与。上层用 IMaskBuilder 生成。"""
        native = mask._native if hasattr(mask, "_native") else mask
        return self._native.setMask(native)

    def clearMask(self):
        """清除掩码, 恢复全参与。"""
        return self._native.clearMask()

    def setSubPixel(self, enable):
        """亚像素拟合开关 (精匹配 3×3 二次曲面驻点), 默认 True。"""
        return self._native.setSubPixel(enable)

    def setPrevPosition(self, px, py):
        """设置上次匹配位置 (coarseMap 像素坐标, 从 getResult 的 px/py 取)。
        配合 setRoughSearchRadius 自动限定粗匹配 ROI。"""
        return self._native.setPrevPosition(px, py)

    def clearPrevPosition(self):
        """清除上次匹配位置, 恢复全图搜索。"""
        return self._native.clearPrevPosition()

    # ── 执行 ──

    def match(self, minimap):
        """在全地图中定位小地图, 返回 1=命中 0=未命中。"""
        native = minimap._native if hasattr(minimap, '_native') else minimap
        return self._native.match(native)

    # ── 结果 ──

    def getResult(self):
        """取定位结果, 返回 _pw.MapMatchResult (.px/.py/.score/.layerIndex), 失败 None。"""
        out = _pw.MapMatchResult()
        if not self._native.getResult(out):
            return None
        return out

    def getMatchTimeMs(self):
        """最近一次 match 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── IMaskBuilder ──────────────────────────────


class IMaskBuilder:
    """通用掩码生成算子封装 (AvoxVision.h, avox_opencv 插件)。

    朴素几何/颜色/位运算, 无场景知识; 返回单通道 IImageBuffer (255=保留/0=排除)。
    供 IMapMatcher.setMask 等使用; 场景特化的掩码组合逻辑在上层。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createMaskBuilder()

    @staticmethod
    def _n(buf):
        """解包 IImageBuffer 代理 → native (已是 native 则原样)。"""
        return buf._native if hasattr(buf, "_native") else buf

    def buildSectorMask(self, w, h, cx, cy, rx, ry, startAngle, endAngle):
        """椭圆扇形掩码: w×h 画布, 圆心(cx,cy), 半轴(rx,ry), [startAngle,endAngle]度
        (0=右/东 顺时针, OpenCV 椭圆约定), 255 实心。"""
        return self._native.buildSectorMask(w, h, cx, cy, rx, ry, startAngle, endAngle)

    def buildCircleMask(self, w, h, cx, cy, radius):
        """圆形实心掩码。"""
        return self._native.buildCircleMask(w, h, cx, cy, radius)

    def buildColorRangeMask(self, src, cs, lo0, lo1, lo2, hi0, hi1, hi2):
        """颜色范围掩码 (inRange); cs=ColorSpace 决定 (lo0,lo1,lo2)/(hi0,hi1,hi2) 通道含义。"""
        return self._native.buildColorRangeMask(self._n(src), cs, lo0, lo1, lo2, hi0, hi1, hi2)

    def maskAnd(self, a, b):
        """位与 (a/b 尺寸需一致, 否则返回 None)。"""
        return self._native.maskAnd(self._n(a), self._n(b))

    def maskOr(self, a, b):
        """位或 (a/b 尺寸需一致, 否则返回 None)。"""
        return self._native.maskOr(self._n(a), self._n(b))

    def maskNot(self, a):
        """位非。"""
        return self._native.maskNot(self._n(a))

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── IYoloDetector ──────────────────────────────────────


@dataclass
class Box:
    """单个检测结果 (原图坐标, 已反 letterbox)。"""
    x1: float
    y1: float
    x2: float
    y2: float
    score: float
    name: str


class IYoloDetector:
    """通用 Ultralytics YOLO 检测/分类封装 (AvoxVision.h, avox_cv 插件)。

    类名/任务/输入尺寸从模型元数据读取, avox 不内置场景知识。首次 detect/classify 按需加载模型。
    detect 用 letterbox; classify 用精确 resize。输出布局 [1,4+nc,N], 不做 sigmoid (已 bake 进图)。
    """

    def __init__(self, native=None):
        self._native = native if native else _pw.createYoloDetector()

    # ── 配置 ──

    def setModelPath(self, path):
        """ONNX 模型路径 (运行期路径, 必填)。"""
        return self._native.setModelPath(path)

    def setUseGpu(self, enable):
        """GPU 推理, 默认 False (CPU)。"""
        return self._native.setUseGpu(enable)

    def setConfThreshold(self, conf):
        """检测置信度阈值, 默认 0.3。"""
        return self._native.setConfThreshold(conf)

    def setIouThreshold(self, iou):
        """NMS IoU 阈值, 默认 0.45。"""
        return self._native.setIouThreshold(iou)

    # ── 执行 ──

    def detect(self, scene, conf=None, iou=None):
        """检测: 返回 {类名: [Box, ...]} (每类内按 score 降序)。conf/iou 传 None 用构造时默认值。"""
        if conf is not None:
            self._native.setConfThreshold(conf)
        if iou is not None:
            self._native.setIouThreshold(iou)
        native = scene._native if hasattr(scene, '_native') else scene
        self._native.detect(native)
        out = {}
        for i in range(self._native.getDetectionCount()):
            b = _pw.YoloBox()
            if not self._native.getDetection(i, b):
                continue
            name = self._native.getClassName(b.classId)
            box = Box(b.x1, b.y1, b.x2, b.y2, b.score, name)
            out.setdefault(name, []).append(box)
        for v in out.values():
            v.sort(key=lambda bx: bx.score, reverse=True)
        return out

    def classify(self, scene):
        """分类: 返回 (类名, 分数), 失败 (模型不匹配/无类名) 返回 None。"""
        native = scene._native if hasattr(scene, '_native') else scene
        classId = self._native.classify(native)
        if classId is None or classId < 0:
            return None
        name = self._native.getClassName(classId)
        if not name:
            return None
        return (name, self._native.getClassifyScore())

    # ── 元信息 ──

    def getTask(self):
        """模型任务 ("detect"/"classify"); 触发懒加载。"""
        return self._native.getTask()

    def getClassCount(self):
        """类别数; 触发懒加载。"""
        return self._native.getClassCount()

    def getClassName(self, classId):
        """类名 (元数据透传, '_' 已转空格); 越界返回空串。"""
        return self._native.getClassName(classId)

    def getDetectionCount(self):
        """最近一次 detect 的框数。"""
        return self._native.getDetectionCount()

    def getDetection(self, index):
        """取第 index 个检测框 (支持负索引), 返回 (YoloBox, 类名) 或 None。"""
        b = _pw.YoloBox()
        if not self._native.getDetection(index, b):
            return None
        return (b, self._native.getClassName(b.classId))

    def getClassifyScore(self):
        """最近一次 classify 的分数。"""
        return self._native.getClassifyScore()

    def getMatchTimeMs(self):
        """最近一次 detect/classify 耗时 (ms)。"""
        return self._native.getMatchTimeMs()

    def getLastError(self):
        """获取最近一次错误信息。"""
        return self._native.getLastError()

    # ── 生命周期 ──

    def destroy(self):
        self._native = None


# ─── 窗口辅助 ────────────────────────────────────────────


def findWindowByName(name):
    """按名称查找窗口, 返回 hwnd。"""
    return _pw.findWindowByName(name)


def getActiveWindow():
    """获取当前活动窗口 hwnd。"""
    return _pw.getActiveWindow()


def getWindowName(hwnd):
    """获取窗口名称。"""
    return _pw.getWindowName(hwnd)


# ─── 模块级工厂函数 ─────────────────────────────────────


def createTemplateMatcher():
    """创建 ITemplateMatcher 封装实例 (avox_opencv 未启用返回 None 降级)。"""
    native = _pw.createTemplateMatcher()
    return ITemplateMatcher(native) if native else None


def createTextRecognizer():
    """创建 ITextRecognizer 封装实例 (avox_ocr 未启用返回 None 降级)。"""
    native = _pw.createTextRecognizer()
    return ITextRecognizer(native) if native else None


def createFeatureMatcher():
    """创建 IFeatureMatcher 封装实例 (avox_opencv 未启用返回 None 降级)。"""
    native = _pw.createFeatureMatcher()
    return IFeatureMatcher(native) if native else None


def createColorDetector():
    """创建 IColorDetector 封装实例 (avox_opencv 未启用返回 None 降级)。"""
    native = _pw.createColorDetector()
    return IColorDetector(native) if native else None


def createOrientationDetector():
    """创建 IOrientationDetector 封装实例 (avox_opencv 未启用返回 None 降级)。"""
    native = _pw.createOrientationDetector()
    return IOrientationDetector(native) if native else None


def createMapMatcher():
    """创建 IMapMatcher 封装实例 (avox_opencv 未启用返回 None 降级)。

    通用小地图定位引擎: 粗匹配 + 精匹配 + 亚像素; 朝向用 IOrientationDetector, 掩码用 IMaskBuilder。
    """
    native = _pw.createMapMatcher()
    return IMapMatcher(native) if native else None


def createMaskBuilder():
    """创建 IMaskBuilder 封装实例 (avox_opencv 未启用返回 None 降级)。

    通用掩码生成算子: 几何(扇形/圆) + 颜色范围(inRange) + 位运算(与/或/非)。
    """
    native = _pw.createMaskBuilder()
    return IMaskBuilder(native) if native else None


def createYoloDetector(modelPath, conf=0.3, iou=0.45, useGpu=False):
    """创建通用 YOLO 检测/分类封装实例 (avox_cv 未启用返回 None 降级)。

    modelPath: Ultralytics 导出的 ONNX 路径。绝对路径 (D:/...) 原样用; 相对路径按 assets
    模型根解析为 models/<path>。类名/任务/尺寸从元数据读取。
    conf: 检测置信度阈值 (默认 0.3); iou: NMS IoU 阈值 (默认 0.45); useGpu: GPU 推理 (默认 False)。
    """
    native = _pw.createYoloDetector()
    if not native:
        return None
    native.setModelPath(modelPath)
    native.setConfThreshold(conf)
    native.setIouThreshold(iou)
    native.setUseGpu(useGpu)
    return IYoloDetector(native)
