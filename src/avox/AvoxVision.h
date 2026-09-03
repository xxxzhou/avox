#pragma once

#include <stdint.h>

#include "AvoxDef.h"
#include "AvoxVideo.h"
#include "AvoxMath.h"
#include "AvoxBase.h"  // ModelLevel (IWatermarkRemoval 用)

namespace avox {

// ============== 模板匹配 (Template / Icon Matching) ==============
// 在场景图 (IImageBuffer) 中定位图标/模板的位置, 供 Agent 点击或质检。
// 实现: plugins/avox_opencv/TemplateMatcher (cv::matchTemplate + NMS)
// 算法参考: MaaFramework/source/MaaFramework/Vision/TemplateMatcher

// 匹配方法 (对应 cv::TemplateMatchModes 的归一化变体)
enum class TemplateMatchMethod {
  none = 0,          // 未设置 (实现回退默认 ccoeffNormed)
  sqdiffNormed = 1,  // TM_SQDIFF_NORMED: 支持 green_mask
  ccorrNormed = 2,   // TM_CCORR_NORMED: 支持 green_mask
  ccoeffNormed = 3,  // TM_CCOEFF_NORMED: 光照鲁棒 (默认); 不支持 green_mask
};

// 结果排序
enum class MatchOrderBy {
  none = 0,
  horizontal,  // 从左到右 (默认, 贴合阅读顺序)
  vertical,    // 从上到下
  score,       // 置信度降序
  area,        // 面积降序
};

// 单次命中结果 (POD, 跨 DLL 安全; 坐标为 scene 坐标系, 见 getMatch)
struct MatchResult {
  int32_t x = 0;               // 命中框左上角 x
  int32_t y = 0;               // 命中框左上角 y
  int32_t w = 0;               // 框宽 (= 模板宽)
  int32_t h = 0;               // 框高 (= 模板高)
  int32_t templateIndex = -1;  // 命中的模板下标 (addTemplate 返回值); 多模板时区分来源
  double score = 0.0;          // 置信度 [0,1], 越大越好
};

// 图标/模板匹配接口
// 跨 DLL 安全: 方法签名只用 IImageBuffer* + 原始类型/枚举, 不传 STL
// (见 doc/plan/动态加载组件设计.md §6); 结果用多段 getter 取出。
// 范本: IWatermarkRemoval (本文件, 见下方水印去除段)
class ITemplateMatcher {
 public:
  virtual ~ITemplateMatcher() = default;

  // 配置 (调用顺序无关, match 前设置即可)
  // 匹配方法, 默认 ccoeffNormed
  virtual void setMethod(TemplateMatchMethod method) = 0;
  // 结果排序, 默认 horizontal
  virtual void setOrderBy(MatchOrderBy orderBy) = 0;
  // 绿色掩码: true=模板中纯绿 (0,255,0) 区域当透明不参与匹配。
  // 仅 sqdiffNormed/ccorrNormed 生效; 若 method=ccoeffNormed 且模板含绿色,
  // 内部自动降级为 ccorrNormed 并记入 getLastError()
  virtual void setGreenMask(bool enable) = 0;
  // 灰度匹配: true=scene 和模板都转单通道灰度再匹配, 对光照/颜色变化更鲁棒。
  // 适合小图标 (如地图花图标); grayscale 模式下 greenMask 被忽略
  virtual void setGrayscale(bool enable) = 0;
  // NMS 去重 IoU 阈值, 默认 0.2; 设 0 关闭 (重叠峰重复返回)
  virtual void setNmsIoU(float iou) = 0;
  // 感兴趣区域, 默认 = 整图
  virtual void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) = 0;
  virtual void clearRoi() = 0;

  // 模板 (可添加多个, 每个独立阈值)
  // threshold 为命中阈值 [0,1], 越大越严 (推荐 0.7~0.8); 返回模板下标 (>=0), 失败 -1
  virtual int32_t addTemplate(IImageBuffer* tmpl, double threshold) = 0;
  // 便捷重载: 从图片文件路径加模板 (内部 createImageBuffer + loadImagePath)
  virtual int32_t addTemplatePath(const char* path, double threshold) = 0;
  virtual void clearTemplates() = 0;

  // 执行: 在 scene 中匹配所有已添加模板, 内部 NMS 去重 + 排序;
  // 返回过滤后命中数量; 同一 matcher 可换 scene 反复 match
  virtual int32_t match(IImageBuffer* scene) = 0;

  // 结果 (index 支持 Python 负索引, -1=最后一个; 越界返回 false)
  virtual int32_t getMatchCount() = 0;
  // 取第 index 个命中 (坐标为 scene 坐标系; 框中心用 getMatchCenter 取供 Agent 点击; templateIndex 标明来自哪个模板)
  virtual bool getMatch(int32_t index, MatchResult* out) = 0;
  // 最近一次 match 耗时 (ms)
  virtual float getMatchTimeMs() = 0;
  // 最近一次错误/警告 (返回内部常量串, 无需释放; 无错为空串)
  virtual const char* getLastError() = 0;
};

// ============== OCR 文字识别 (Text Recognition) ==============
// 在场景图 (IImageBuffer) 中识别文字及其位置, 供 Agent 按文字查找并点击。
// 实现: plugins/avox_ocr/TextRecognizer (PP-OCRv6 det + rec, 推理走 avox_onnx 的 IONNXSession)
// 后处理参考: RapidAI/RapidOcrOnnx

// 单条 OCR 结果 (POD, 跨 DLL 安全; 坐标为 scene 坐标系)
struct OcrResult {
  int32_t x = 0;          // 文字框左上角 x
  int32_t y = 0;          // 文字框左上角 y
  int32_t w = 0;          // 文字框宽
  int32_t h = 0;          // 文字框高
  double score = 0.0;     // 识别置信度 [0,1]
};

// OCR 文字识别接口 (拉模式, 同 ITemplateMatcher: recognize 一次, getMatch 循环取)
// 跨 DLL 安全: 仅传 IImageBuffer* + 原始类型 + POD OcrResult; 文字串由 getMatch 返回 const char*
class ITextRecognizer {
 public:
  virtual ~ITextRecognizer() = default;

  // 配置 (调用顺序无关, recognize 前设置即可)
  // 检测阈值 [0,1], 默认 0.3 (DBNet 概率图二值化阈值)
  virtual void setThreshold(double threshold) = 0;
  // 感兴趣区域, 默认 = 整图
  virtual void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) = 0;
  virtual void clearRoi() = 0;
  // GPU 推理, 默认 false (CPU)
  virtual void setUseGpu(bool enable) = 0;

  // 执行: det + rec 全图识别 (首次自动按需加载模型); 返回文字框数; 同一 recognizer 可换 scene 反复 recognize
  virtual int32_t recognize(IImageBuffer* scene) = 0;

  // 结果 (index 支持 Python 负索引, -1=最后一个; 越界返回 nullptr)
  virtual int32_t getMatchCount() = 0;
  // 取第 index 个识别文字 (UTF-8, 返回内部常量串无需释放; out 非空时写入框坐标, 越界返回 nullptr)
  virtual const char* getMatch(int32_t index, OcrResult* out) = 0;
  // 最近一次 recognize 耗时 (ms)
  virtual float getMatchTimeMs() = 0;
  // 最近一次错误/警告 (返回内部常量串, 无需释放; 无错为空串)
  virtual const char* getLastError() = 0;
};

// ============== 特征匹配 (Feature Matching) ==============
// 在场景图中用关键点描述子(SIFT/ORB/AKAZE)定位参考图。对旋转/尺度/光照鲁棒,
// 与 ITemplateMatcher 互补, 适合定位/拼接。实现: plugins/avox_opencv/FeatureMatcher
// (SIFT/ORB/AKAZE + BFMatcher + Lowe 比率 + RANSAC 单应性)

// 关键点描述子方法
enum class FeatureMethod {
  sift = 0,   // cv::SIFT (默认; 旋转/尺度不变, 适合定位)
  orb = 1,    // cv::ORB (更快, 二值描述子)
  akaze = 2,  // cv::AKAZE (旋转不变, 精度/速度居中)
};

// 单次命中结果 (POD, 跨 DLL 安全; 坐标为 scene 坐标系)
struct FeatureMatchResult {
  int32_t x = 0;            // 参考图在 scene 中的命中框左上角 x
  int32_t y = 0;
  int32_t w = 0;            // 框宽 (≈参考图宽, 经单应性投影后可能略变)
  int32_t h = 0;
  int32_t refIndex = -1;    // 命中的参考图下标 (addReference 返回值)
  double score = 0.0;       // 内点比例 [0,1] (RANSAC inliers / 总匹配), 越大越可信
};

// 特征匹配接口 (拉模式, 同 ITemplateMatcher: match 一次, getMatch 循环取)
// 跨 DLL 安全: 方法签名只用 IImageBuffer* + 原始类型/枚举, 不传 STL
class IFeatureMatcher {
 public:
  virtual ~IFeatureMatcher() = default;

  // 配置 (调用顺序无关, match 前设置即可)
  virtual void setMethod(FeatureMethod method) = 0;       // 默认 sift
  virtual void setMaxFeatures(int32_t n) = 0;              // 检测器上限, 默认 1000
  virtual void setRatioThreshold(float ratio) = 0;         // Lowe 比率测试阈值, 默认 0.75f
  virtual void setMinInliers(int32_t n) = 0;               // 接受命中的最小内点数, 默认 8
  virtual void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) = 0;  // scene 上 ROI, 默认整图
  virtual void clearRoi() = 0;

  // 参考图 (要定位的"小图", 可多个, 每个独立检测)
  virtual int32_t addReference(IImageBuffer* ref) = 0;          // 返回下标 >=0, 失败 -1
  virtual int32_t addReferencePath(const char* path) = 0;       // 便捷重载: 内部 createImageBuffer + loadImagePath
  virtual void clearReferences() = 0;

  // 执行: 在 scene 中定位所有参考图; 返回过滤后命中数 (按 score 降序)
  virtual int32_t match(IImageBuffer* scene) = 0;

  // ---- BGI 大地图 SIFT 定位 (train=预存底图切块, query=视口实时算) ----
  // 独立于 match() 的"小 ref 在大 scene"语义; 中心点定位: query 投影到 train 得坐标。
  // 实现内部强制 SIFT 无参 + 内嵌 BGI 常量, 不读 setMethod/setMaxFeatures 等配置。
  // 详见 plugins/avox_opencv/FeatureMatcher (loadTrainFeaturesPath/matchQueryLocal/matchQueryFull)。

  // 一次性加载预存底图特征并切块缓存 (BGI 格式: kpPath=.kp.bin=N×28B cv::KeyPoint,
  // descPath=.mat.png=128×N 灰度)。imgW/imgH=底图像素, blockRows/blockCols=切块网格。
  // 重复调用替换缓存。失败返回 false (查 getLastError)。
  virtual bool loadTrainFeaturesPath(const char* kpPath, const char* descPath,
                                     int32_t imgW, int32_t imgH,
                                     int32_t blockRows, int32_t blockCols) = 0;

  // query(视口) 局部定位: roi=train 坐标系预期搜索矩形 (query 中心预期落点周围),
  // 取覆盖格子 ±expandCells 做 FLANN knnMatch (BGI KnnMatchLocal); good 不足/失败时内部回退全图。
  // 结果经 getMatch(0): .x/.y=query 中心在 train 坐标, .w/.h=0 (点结果哨兵), .score=inliers/good。
  // 返回 1=命中 / 0=未定位 (查 getLastError)。
  virtual int32_t matchQueryLocal(IImageBuffer* query,
                                  int32_t roiX, int32_t roiY, int32_t roiW, int32_t roiH,
                                  int32_t expandCells) = 0;

  // query(视口) 全图兜底定位 (BGI Match: FLANN 单最佳 + 距离阈值 + RANSAC)。输出约定同 matchQueryLocal。
  virtual int32_t matchQueryFull(IImageBuffer* query) = 0;

  // 结果 (index 支持 Python 负索引, -1=最后一个; 越界返回 false)
  virtual int32_t getMatchCount() = 0;
  virtual bool getMatch(int32_t index, FeatureMatchResult* out) = 0;
  virtual float getMatchTimeMs() = 0;
  virtual const char* getLastError() = 0;
};

// ============== 颜色区域检测 (Color Region Detection) ==============
// 在场景图中检测落在指定颜色范围内的连通区域, 返回各区域包围盒/面积。
// 实现: plugins/avox_opencv/ColorDetector
// (cv::inRange + connectedComponentsWithStats)

// 颜色空间 (决定 setRange 三通道含义)
enum class ColorSpace {
  bgr = 0,   // 默认 (与屏幕捕获 bgra8→bgr 一致)
  rgb = 1,
  hsv = 2,   // 色相检测(对亮度变化鲁棒); H 范围 0-179 (OpenCV 约定)
};

// 单个颜色区域 (POD, 跨 DLL 安全; 坐标为 scene 坐标系)
struct ColorRegion {
  int32_t x = 0;       // 包围盒左上角 x
  int32_t y = 0;
  int32_t w = 0;       // 包围盒宽
  int32_t h = 0;
  int32_t area = 0;    // 区域像素数 (连通块大小)
  double score = 0.0;  // 填充率 = area / (w*h), 越大越实心
};

// 颜色区域检测接口 (拉模式)
// 跨 DLL 安全: 仅传 IImageBuffer* + 原始类型 + POD ColorRegion
class IColorDetector {
 public:
  virtual ~IColorDetector() = default;

  // 配置 (detect 前设置即可)
  virtual void setColorSpace(ColorSpace cs) = 0;
  // 颜色范围 [c0Min..c0Max]×[c1Min..c1Max]×[c2Min..c2Max], 三通道顺序由 ColorSpace 决定
  virtual void setRange(int32_t c0Min, int32_t c1Min, int32_t c2Min,
                        int32_t c0Max, int32_t c1Max, int32_t c2Max) = 0;
  virtual void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) = 0;  // 默认整图
  virtual void clearRoi() = 0;
  virtual void setMinArea(int32_t area) = 0;   // 丢弃小于此面积的区域, 默认 8
  virtual void setMaxRegions(int32_t n) = 0;   // 返回上限 (按面积降序截断), 默认 100

  // 执行: 返回区域数; 同一 detector 可换 scene 反复 detect
  virtual int32_t detect(IImageBuffer* scene) = 0;

  // 结果 (index 支持 Python 负索引; 越界返回 false)
  virtual int32_t getRegionCount() = 0;
  virtual bool getRegion(int32_t index, ColorRegion* out) = 0;
  virtual float getMatchTimeMs() = 0;
  virtual const char* getLastError() = 0;
};

// ============== 方向检测 (Orientation Detection) ==============
// 在图像中估计主导方向朝向等。
// 算法: BGI CameraOrientationFromGia 忠实移植 (极坐标展开 + 波峰卷积)。
// 圆心由 setCircle 传入(缺省=图像中心)
// 实现: plugins/avox_opencv/OrientationDetector

// 方向检测接口 (单值结果, 非拉模式)
// 跨 DLL 安全: 仅传 IImageBuffer* + 原始类型
class IOrientationDetector {
 public:
  virtual ~IOrientationDetector() = default;

  // 配置 (compute 前设置即可)
  // 圆形分析区域: 圆心 (cx,cy) + 半径 radius (scene 坐标系)
  virtual void setCircle(int32_t cx, int32_t cy, int32_t radius) = 0;
  // 角度搜索范围 [angleMin, angleMax), 度, 默认 [0,360); 用于限制/分段搜索
  virtual void setAngleRange(int32_t angleMin, int32_t angleMax) = 0;
  // 平滑窗口 (度), 默认 5; 越大越平滑 (抗噪), 牺牲精度
  virtual void setSmooth(int32_t degrees) = 0;

  // 执行: 返回主导角度 (BGI 原始输出, 0=右/东 顺时针, 实际取值 [45,360]; 精确约定待实机标定); 失败返回 -1
  virtual double compute(IImageBuffer* scene) = 0;
  virtual double getLastScore() = 0;  // 峰值强度/置信度 [0,1]
  virtual float getMatchTimeMs() = 0;
  virtual const char* getLastError() = 0;
};

// ============== 地图定位 (Map Position Matching) ==============
// 在全地图中定位小地图位置 (BGI 模板匹配方案: 朝向去旋转 + 粗匹配 + 精匹配)。
// 实现: plugins/avox_opencv/MapMatcher

// 地图定位结果 (POD, 跨 DLL 安全; 坐标为地图图像像素坐标系, 亚像素精度)
struct MapMatchResult {
  double px = 0.0;          // 命中中心 x (地图图像像素坐标)
  double py = 0.0;          // 命中中心 y
  double score = 0.0;       // 置信度 [0,1], 越大越好
  int32_t layerIndex = -1;  // 命中图层索引 (单层模式=0, 未匹配=-1)
};

// 地图定位接口 (通用小地图定位引擎; 算法对照 BGI SceneBaseMapByTemplateMatch)
// 通用: 只认"小地图 + (可选)掩码 + 大地图", 不含任何场景知识。朝向检测/掩码生成/
// 坐标换算由上层负责 (朝向用 IOrientationDetector, 掩码用 IMaskBuilder)。
// 跨 DLL 安全: 方法签名只用 IImageBuffer* + 原始类型/枚举, 不传 STL
class IMapMatcher {
 public:
  virtual ~IMapMatcher() = default;

  // 配置 (match 前设置即可)
  // 全地图彩图 (粗匹配用; BGI *_color.webp)
  virtual void setMapImage(IImageBuffer* mapImg) = 0;
  // 全地图彩图 (从文件加载)
  virtual void setMapImageByPath(const char* path) = 0;
  // 灰度地图图 (精匹配用; BGI *_gray.webp)
  virtual void setFineMapImage(IImageBuffer* mapImg) = 0;
  virtual void setFineMapImageByPath(const char* path) = 0;
  // 感兴趣区域 (全地图上的搜索区域; 有 prev_position 时缩小搜索范围)
  virtual void setRoi(int32_t x, int32_t y, int32_t w, int32_t h) = 0;
  virtual void clearRoi() = 0;
  // 二值掩码 (单通道, 255=参与比对/0=排除); 由上层生成 (如 IMaskBuilder)。
  // 未设 = 全图参与。掩码尺寸应与小地图一致, 内部按 coarse/exact 尺寸缩放
  virtual void setMask(IImageBuffer* mask) = 0;
  virtual void clearMask() = 0;
  // 最低置信度, 默认 0.95
  virtual void setMinScore(double score) = 0;
  // 粗匹配时小地图缩放尺寸, 默认 52
  virtual void setCoarseSize(int32_t size) = 0;
  // 精匹配时小地图尺寸, 默认 260
  virtual void setExactSize(int32_t size) = 0;
  // 局部搜索半径 (精匹配, fineMap 像素, 有 prev_position 时), 默认 50
  virtual void setSearchRadius(int32_t radius) = 0;
  // 粗匹配局部搜索半径 (coarseMap 像素; 配合 setPrevPosition 自动限定粗匹配 ROI 范围)
  // 默认 0 = 不限制 (全图搜索); >0 时以 prevPosition 为中心限定粗匹配搜索区域
  virtual void setRoughSearchRadius(int32_t radius) = 0;
  // 亚像素拟合 (精匹配结果 3×3 邻域二次曲面极值), 默认 true
  virtual void setSubPixel(bool enable) = 0;
  // 上次匹配位置 (coarseMap 像素坐标, 从 getResult 的 px/py 取)
  // 配合 setRoughSearchRadius 自动限定粗匹配 ROI; 未设=全图搜索
  virtual void setPrevPosition(double px, double py) = 0;
  virtual void clearPrevPosition() = 0;

  // 执行: 在全地图中定位小地图 (粗匹配 + 精匹配 + 亚像素); 返回 1=命中, 0=未命中
  virtual int32_t match(IImageBuffer* minimap) = 0;

  // 结果
  virtual bool getResult(MapMatchResult* out) = 0;
  virtual float getMatchTimeMs() = 0;
  virtual const char* getLastError() = 0;
};

// ============== 掩码生成 (Mask Generation) ==============
// 通用二值掩码生成算子 (返回单通道 IImageBuffer, 255=保留/0=排除), 供 IMapMatcher.setMask 等使用。
// 不含任何场景知识: 角度/颜色/半径全由调用方传入; 场景特化的掩码组合逻辑在上层。
// 实现: plugins/avox_opencv/MaskBuilder

// 掩码生成接口 (无状态算子集; 跨 DLL 安全, 仅 IImageBuffer* + 原始类型/枚举)
// 返回的 IImageBuffer* 由调用方释放 (Python 封装自动管理)
class IMaskBuilder {
 public:
  virtual ~IMaskBuilder() = default;

  // 椭圆扇形掩码: w×h 画布, 圆心 (cx,cy), 半轴 (rx,ry),
  // 填充 [startAngle,endAngle] 度扇形 (OpenCV 椭圆角度约定, 0=右/东 顺时针)。白色 (255) 实心。
  virtual IImageBuffer* buildSectorMask(int32_t w, int32_t h, int32_t cx, int32_t cy,
                                        int32_t rx, int32_t ry, double startAngle,
                                        double endAngle) = 0;
  // 圆形实心掩码: w×h 画布, 圆心 (cx,cy), 半径 radius
  virtual IImageBuffer* buildCircleMask(int32_t w, int32_t h, int32_t cx, int32_t cy,
                                        int32_t radius) = 0;
  // 颜色范围掩码 (inRange): src 落在 [lo,hi] 三通道范围内的像素置 255。
  // cs 决定通道含义 (复用 ColorSpace: bgr/rgb/hsv; hsv 的 H 范围 0-179)
  virtual IImageBuffer* buildColorRangeMask(IImageBuffer* src, ColorSpace cs,
                                            int32_t lo0, int32_t lo1, int32_t lo2,
                                            int32_t hi0, int32_t hi1, int32_t hi2) = 0;
  // 位运算组合 (a/b 尺寸需一致, 否则返回 nullptr)
  virtual IImageBuffer* maskAnd(IImageBuffer* a, IImageBuffer* b) = 0;
  virtual IImageBuffer* maskOr(IImageBuffer* a, IImageBuffer* b) = 0;
  virtual IImageBuffer* maskNot(IImageBuffer* a) = 0;
  // 最近一次错误 (返回内部常量串, 无需释放; 无错为空串)
  virtual const char* getLastError() = 0;
};

// ============== 通用 YOLO 检测/分类 (YOLO Detection / Classification) ==============
// 通用 Ultralytics YOLO ONNX (检测或分类) 推理+解码。类名从模型元数据透传,
// avox 不内置任何场景知识 (喂 COCO 返回 person/car..., 喂自训练模型返回该模型的类)。
// 实现: plugins/avox_cv/yolo/YoloDetector (letterbox + onnx + 按类 NMS; 走 avox_onnx 的 IONNXSession)
// 解码参考: avox_genshin/src/abilities/detector.py (标准 Ultralytics 格式, 不做 sigmoid)

// 单个检测结果 (POD, 跨 DLL 安全; 原图坐标系, 已反 letterbox)
struct YoloBox {
  float x1 = 0;          // 左上 x
  float y1 = 0;          // 左上 y
  float x2 = 0;          // 右下 x
  float y2 = 0;          // 右下 y
  double score = 0.0;    // 置信度 [0,1]
  int32_t classId = -1;  // 类下标 (类名经 getClassName 取)
};

// 通用 YOLO 接口 (拉模式: set* 配置 → detect/classify → getDetection 循环取)
// 跨 DLL 安全: 方法签名只用 IImageBuffer* + 原始类型/POD, 不传 STL; 类名/错误串走 const char*
class IYoloDetector {
 public:
  virtual ~IYoloDetector() = default;

  // 配置 (首次推理前设置即可)
  // 必填: ONNX 模型路径。绝对路径 (D:/, /) 原样用; 相对路径按 assets 模型根解析为 models/<path>
  virtual void setModelPath(const char* path) = 0;
  // GPU 推理, 默认 false (CPU)
  virtual void setUseGpu(bool enable) = 0;
  // 置信度阈值 [0,1], 默认 0.3
  virtual void setConfThreshold(float conf) = 0;
  // NMS IoU 阈值 [0,1], 默认 0.45
  virtual void setIouThreshold(float iou) = 0;

  // 模型元数据 (首次推理时懒加载; 未加载返回空串/0)
  // 任务类型 "detect" / "classify" (从 ONNX 元数据 task 字段)
  virtual const char* getTask() = 0;
  // 类别数
  virtual int32_t getClassCount() = 0;
  // 类名 (返回内部常量串无需释放; _ 已转空格; 越界返回空串)
  virtual const char* getClassName(int32_t classId) = 0;

  // 检测 (detect 模型): 返回过滤后框数; 同一 detector 可换 scene 反复 detect
  virtual int32_t detect(IImageBuffer* scene) = 0;
  // 结果 (index 支持 Python 负索引; 越界返回 false)
  virtual int32_t getDetectionCount() = 0;
  virtual bool getDetection(int32_t index, YoloBox* out) = 0;

  // 分类 (classify 模型): 返回 top 类下标 (失败 -1); 置信度经 getClassifyScore 取
  virtual int32_t classify(IImageBuffer* scene) = 0;
  virtual float getClassifyScore() = 0;

  // 最近一次 detect/classify 耗时 (ms)
  virtual float getMatchTimeMs() = 0;
  // 最近一次错误/警告 (返回内部常量串, 无需释放; 无错为空串)
  virtual const char* getLastError() = 0;
};

// ============== 水印去除 (Watermark Removal / Inpaint) ==============
// 图像水印检测 + 修复。实现: plugins/avox_cv (LaMa / AOT-GAN, 走 avox_onnx 的 IONNXSession)
// 范本: ITemplateMatcher (跨 DLL 安全, 仅 IImageBuffer* + 原始类型/枚举)

// 修复模式
enum class InpaintMode {
  lama = 0,   // LaMa 模型 (默认)
  aotgan = 1  // AOT-GAN 模型
};

class IWatermarkRemoval {
 public:
  virtual ~IWatermarkRemoval() = default;

  // 配置
  virtual void setModelLevel(ModelLevel level) = 0;
  virtual void setInpaintMode(InpaintMode mode) = 0;
  virtual void setMaskDilate(int dilatePixels) = 0;
  virtual void setDetectThreshold(float threshold) = 0;
  virtual void setUseGPU(bool useGPU) = 0;

  // 生命周期: open=按配置(setInpaintMode/setModelLevel)经缓存选模型加载(幂等); close=清指针/缓冲(不释放共享模型); ready=已就绪
  virtual bool open() = 0;
  virtual void close() = 0;
  virtual bool ready() = 0;

  // 处理接口
  virtual bool process(IImageBuffer* input, IImageBuffer* output) = 0;
  virtual bool detect(IImageBuffer* input, IImageBuffer* maskOutput) = 0;
  virtual bool inpaint(IImageBuffer* input, IImageBuffer* mask,
                       IImageBuffer* output) = 0;

  // 结果信息
  virtual int getWatermarkCount() = 0;
  virtual bool getWatermarkBBox(int index, float* x, float* y, float* w,
                                float* h) = 0;
  virtual float getDetectTimeMs() = 0;
  virtual float getInpaintTimeMs() = 0;
};

extern "C" {
// 创建 OCR 识别实例 (经 textRecognizerHub.create("ppocr"); avox_ocr 未启用返回 nullptr 降级)
AVOX_EXPORT ITextRecognizer* createTextRecognizer();
// 创建模板匹配实例 (经 templateMatcherHub.create("opencv"); avox_opencv 未启用返回 nullptr 降级)
AVOX_EXPORT ITemplateMatcher* createTemplateMatcher();
// 取命中框中心 (供 Agent 点击); scene 坐标系
AVOX_EXPORT vec2i getMatchCenter(const MatchResult& r);
// 取文字框中心 (供 Agent 点击); scene 坐标系
AVOX_EXPORT vec2i getOcrCenter(const OcrResult& r);
// 创建特征匹配实例 (经 featureMatcherHub.create("opencv"); avox_opencv 未启用返回 nullptr 降级)
AVOX_EXPORT IFeatureMatcher* createFeatureMatcher();
// 创建颜色区域检测实例 (经 colorDetectorHub.create("opencv"); 未启用返回 nullptr 降级)
AVOX_EXPORT IColorDetector* createColorDetector();
// 创建方向检测实例 (经 orientationDetectorHub.create("opencv"); 未启用返回 nullptr 降级)
AVOX_EXPORT IOrientationDetector* createOrientationDetector();
// 取特征匹配命中框中心 (scene 坐标系, 供点击/定位)
AVOX_EXPORT vec2i getFeatureMatchCenter(const FeatureMatchResult& r);
// 取颜色区域中心 (scene 坐标系)
AVOX_EXPORT vec2i getColorRegionCenter(const ColorRegion& r);
// 创建地图定位实例 (经 mapMatcherHub.create("opencv"); avox_opencv 未启用返回 nullptr 降级)
AVOX_EXPORT IMapMatcher* createMapMatcher();
// 创建掩码生成实例 (经 maskBuilderHub.create("opencv"); avox_opencv 未启用返回 nullptr 降级)
AVOX_EXPORT IMaskBuilder* createMaskBuilder();
// 创建通用 YOLO 检测/分类实例 (经 yoloDetectorHub.create("yolo"); avox_cv 未启用返回 nullptr 降级)
AVOX_EXPORT IYoloDetector* createYoloDetector();
}

}
