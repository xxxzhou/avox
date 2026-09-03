#pragma once

#include "VkExport.h"
#include "avox/AvoxLayer.h"
#include "avox/layer/AvoxNode.hpp"
#include "avox/AvoxMath.h"

namespace avox {

enum class ConvertType : int32_t { other = 0, rgba82rgba32f, rgba32f2rgba8, rgba16f2rgba32f, rgba32f2rgba16f };

enum class ReduceOperate : int32_t {
  sum,
  min,
  max,
};

struct KernelSizeParamet {
  int32_t kernelSizeX = 5;
  int32_t kernelSizeY = 5;

  inline bool operator==(const KernelSizeParamet& right) const {
    return this->kernelSizeX == right.kernelSizeX &&
           this->kernelSizeY == right.kernelSizeY;
  }
};

struct GaussianBlurParamet {
  int32_t blurRadius = 4;
  // sigma值越小,整个分布长度范围越大,原始值占比越高,周围占比越低
  // 如果为0,根据blurRadius自动计算
  float sigma = 0.0f;

  inline bool operator==(const GaussianBlurParamet& right) const {
    return this->blurRadius == right.blurRadius && this->sigma == right.sigma;
  }
};

// 确定像素周围的局部亮度,比较周边与局部亮度
struct AdaptiveThresholdParamet {
  // 背景平均模糊半径
  int32_t boxSize = 10;
  // 比较平均亮度偏移
  float offset = 0.05f;
};

struct GuidedParamet {
  int32_t boxSize = 10;
  // //0.1-0.0000001
  float eps = 0.000001f;
};

struct GuidedMattingParamet {
  GuidedParamet guided = {};
};

struct HarrisDetectionBaseParamet {
  float edgeStrength = 1.0f;
  GaussianBlurParamet blueParamet = {4, 0.0f};
  // 检测到一个点作为拐角的阈值.
  // 根据尺寸,光线条件和iOS设备相机类型的不同,此方法可能会有很大的不同
  // 因此可能需要一些试验才能确定适合您的情况.默认值为0.20.
  float threshold = 0.2f;

  inline bool operator==(const HarrisDetectionBaseParamet& right) const {
    return this->edgeStrength == right.edgeStrength &&
           this->blueParamet == right.blueParamet &&
           this->threshold == right.threshold;
  }
};

// Harris角点检测
struct HarrisCornerDetectionParamet {
  HarrisDetectionBaseParamet harrisBase = {};
  float harris = 0.04f;
  // 一个内部比例因子,用于调整在滤镜中生成的边角图的动态范围.默认值为5.0.
  float sensitivity = 5.0f;
};

struct NobleCornerDetectionParamet {
  HarrisDetectionBaseParamet harrisBase = {};
  float sensitivity = 5.0f;
};

// 执行Canny边缘阈值检测
struct CannyEdgeDetectionParamet {
  GaussianBlurParamet blueParamet = {4, 0.0f};
  // 任何梯度幅度大于此阈值的边都将通过并显示在最终结果中
  float minThreshold = 0.1f;
  // 任何梯度幅度低于此阈值的边将失败,并从最终结果中删除.
  float maxThreshold = 0.4f;
};

struct FASTFeatureParamet {
  int32_t boxSize = 5;
  float offset = 1.0f;
};

// 双边滤波
struct BilateralParamet {
  // 模糊周边的半径(圆形)
  int32_t kernelSize = 5;
  // 同高斯模糊的sigma,值越小,周边占比越小
  float sigma_spatial = 10.0f;
  // sigma_spatial是距离间系数,sigma_color是颜色差异比较
  // 同上,这值越小,颜色差异大的部分占比小
  float sigma_color = 10.0f;

  inline bool operator==(const BilateralParamet& right) const {
    return this->kernelSize == right.kernelSize &&
           this->sigma_spatial == right.sigma_spatial &&
           this->sigma_color == right.sigma_color;
  }
};

struct CrosshatchParamet {
  float crossHatchSpacing = 0.03f;
  float lineWidth = 0.003f;
  inline bool operator==(const CrosshatchParamet& right) const {
    return this->crossHatchSpacing == right.crossHatchSpacing &&
           this->lineWidth == right.lineWidth;
  }
};

// 用于添加或删除雾度(类似于UV滤镜)
struct HazeParamet {
  // 所应用颜色的强度.默认值为0.最好是-.3和.3之间的值.
  float distance = 0.0f;
  // 颜色变化的量.默认值为0.最好是-.3和.3之间的值.
  float slope = 0.0f;
  inline bool operator==(const HazeParamet& right) const {
    return this->distance == right.distance && this->slope == right.slope;
  }
};

// 调整图像的阴影和高光
struct HighlightShadowParamet {
  // 增加阴影以使阴影变淡,从0.0到1.0,默认值为0.0.
  float shadows = 0.0f;
  // 从1.0降低到0.0,以1.0为默认值将高光变暗.
  float highlights = 1.0f;
  inline bool operator==(const HighlightShadowParamet& right) const {
    return this->shadows == right.shadows &&
           this->highlights == right.highlights;
  }
};

struct IOSBlurParamet {
  float sacle = 4.0f;
  GaussianBlurParamet blurParamet = {12, 0.0f};
  float saturation = 0.8f;
  float range = 0.6f;
};

// 对图像应用定向运动模糊
struct MotionBlurParamet {
  // 模糊大小的倍数,范围从0.0开始,默认为1.0
  float blurSize = 1.0f;
  // 模糊的角度方向,以度为单位.默认情况下为0度.
  float blurAngle = 0.0f;
  inline bool operator==(const MotionBlurParamet& right) const {
    return this->blurSize == right.blurSize &&
           this->blurAngle == right.blurAngle;
  }
};

// 应用两个图像的泊松混合
struct PoissonParamet {
  // 混合范围从0.0(仅图像1)到1.0(仅图像2渐变),以1.0为正常水平
  float percent = 0.5f;
  // 播渐变的次数.如果您想获得接近收敛的效果,则可以将其提高到100甚至1000.是的,这会很慢.
  int32_t iterationNum = 10;
};

// 将图像分成规则网格内的彩色点
struct PolkaDotParamet {
  // 点在每个网格空间中所占的比例从0.0到1.0,默认值为0.9.
  float dotScaling = 0.90f;
  // 点的大小,以图像的宽度和高度的分数为单位(0.0-1.0,默认为0.05)
  float fractionalWidthOfPixel = 0.01f;
  float aspectRatio = 0.5625f;
  inline bool operator==(const PolkaDotParamet& right) const {
    return this->dotScaling == right.dotScaling &&
           this->fractionalWidthOfPixel == right.fractionalWidthOfPixel &&
           this->aspectRatio == right.aspectRatio;
  }
};

// 图像锐化
struct SharpenParamet {
  int32_t offset = 1;
  // 要应用的清晰度调整(-4.0-4.0,默认值为0.0)
  float sharpness = 0.0f;
  inline bool operator==(const SharpenParamet& right) const {
    return this->offset == right.offset && this->sharpness == right.sharpness;
  }
};

// 肤色调整滤镜,可影响浅肤色颜色的唯一范围,并相应地调整粉红色/绿色或粉红色/橙色范围
struct SkinToneParamet {
  // 调整肤色的量.默认值:0.0,建议的最小值/最大值:分别为-0.3和0.3.
  float skinToneAdjust = 0.0f;
  // 要检测的皮肤色调.默认值:0.05(白皙至泛红皮肤).
  float skinHue = 0.05f;
  // 皮肤色调的变化量.默认值:40.0.
  float skinHueThreshold = 40.0f;
  // 允许的最大色相偏移量.默认值:0.25
  float maxHueShift = 0.25f;
  // 要移动的最大饱和量(使用橙色时).默认值:0.4
  float maxSaturationShift = 0.4f;
  // Green/Orange[0,1]
  int32_t upperSkinToneColor = 0;

  inline bool operator==(const SkinToneParamet& right) const {
    return this->skinToneAdjust == right.skinToneAdjust &&
           this->skinHue == right.skinHue &&
           this->skinHueThreshold == right.skinHueThreshold &&
           this->upperSkinToneColor == right.upperSkinToneColor &&
           this->maxSaturationShift == right.maxSaturationShift &&
           this->maxHueShift == right.maxHueShift;
  }
};

// 使用Sobel边缘检测在对象周围放置黑色边框,然后对图像中存在的颜色进行量化,以使图像具有卡通般的质量
struct ToonParamet {
  // 边缘检测的灵敏度,值越小灵敏度越高.范围从0.0到1.0,默认值为0.2
  float threshold = 0.2f;
  // 要在最终图像中表示的色阶数.默认值为10.0
  float quantizationLevels = 10.0f;
  inline bool operator==(const ToonParamet& right) const {
    return this->threshold == right.threshold &&
           this->quantizationLevels == right.quantizationLevels;
  }
};

// 这使用与ToonLayer相似的过程,只是它在卡通效果之前带有高斯模糊用来平滑噪声
struct SmoothToonParamet {
  GaussianBlurParamet blur = {};
  ToonParamet toon = {};
};

struct SoftEleganceParamet {
  GaussianBlurParamet blur = {10, 0.0f};
  float mix = 0.14f;
};

// 执行Sobel边缘阈值检测
struct ThresholdSobelParamet {
  float edgeStrength = 1.0f;
  // 高于此阈值的任何边缘将为黑色,低于白色的任何边缘.
  float threshold = 0.25f;
  inline bool operator==(const ThresholdSobelParamet& right) const {
    return this->edgeStrength == right.edgeStrength &&
           this->threshold == right.threshold;
  }
};

// 模拟倾斜移位镜头效果
struct TiltShiftParamet {
  GaussianBlurParamet blur = {7, 0.0f};
  // 图像中对焦区域顶部的标准化位置,范围0.0-1.0
  float topFocusLevel = 0.4f;
  // 图像中对焦区域底部的标准化位置,范围0.0-1.0,需要高于topFocusLevel
  float bottomFocusLevel = 0.6f;
  // 图像从对焦区域模糊的速率,默认为0.2
  float focusFallOffRate = 0.2f;
};

// 应用不清晰的蒙版
struct UnsharpMaskParamet {
  // 高斯模糊的模糊参数
  GaussianBlurParamet blur = {4, 0.0f};
  // 清晰度,(0.0-1.0),默认1.0
  float intensity = 1.0f;
};

// 调整图像的白平衡.
struct WhiteBalanceParamet {
  // 调整图像所用的温度,以ºK为单位.值4000非常凉爽,而7000非常温暖.默认值为5000.
  float temperature = 5000.0f;
  // 用于调整图像的色调.值-200表示非常绿色,而200表示非常粉红色.默认值为0.
  float tint = 0.0f;
  inline bool operator==(const WhiteBalanceParamet& right) const {
    return this->temperature == right.temperature && this->tint == right.tint;
  }
};

// https://www.unrealengine.com/en-US/tech-blog/setting-up-a-chroma-key-material-in-ue4
struct ChromaKeyParamet {
  // 比较差异,确定使用亮度与颜色比例,值需大于0,值越大,亮度所占比例越大
  float lumaMask = 1.0f;
  // 需要扣除的颜色
  vec3f chromaColor = {};
  // 用环境光补受蓝绿幕影响的像素(简单理解扣像结果要放入的环境光的颜色)
  float ambientScale = 0.f;
  // 环境光颜色
  vec3f ambientColor = {};
  // 比较差异相差的最少值(少于这值会放弃alpha)
  float alphaCutoffMin = 0.2f;
  // 比较后的alpha系数增亮
  float alphaScale = 10.0f;
  // 比较后的alpha指数增亮
  float alphaExponent = 0.1f;
  // 溢漏(蓝绿幕对物体的影响)系数,这部分颜色扣除并用环境补起
  float despillScale = 0.0f;
  // 溢漏(蓝绿幕对物体的影响)指数
  float despillExponent = 0.1f;
  inline bool operator==(const ChromaKeyParamet& right) const {
    return this->lumaMask == right.lumaMask &&
           this->chromaColor == right.chromaColor &&
           this->ambientScale == right.ambientScale &&
           this->ambientColor == right.ambientColor &&
           this->alphaCutoffMin == right.alphaCutoffMin &&
           this->alphaScale == right.alphaScale &&
           this->alphaExponent == right.alphaExponent &&
           this->despillScale == right.despillScale &&
           this->despillExponent == right.despillExponent;
  }
};

// 在图像上创建凸出的失真
struct DistortionParamet {
  float aspectRatio = 0.5625f;
  // 图像的中心(在0-1.0的标准化坐标中),默认值为(0.5,0.5)
  vec2f center = {0.5f, 0.5f};
  // 从中心开始应用变形的半径,默认值为0.25
  float radius = 0.25f;
  // 要应用的失真量,从-1.0到1.0,默认值为0.5
  float scale = 0.5f;

  inline bool operator==(const DistortionParamet& right) const {
    return this->aspectRatio == right.aspectRatio &&
           this->center == right.center && this->radius == right.radius &&
           this->scale == right.scale;
  }
};

// 圆形区域
struct PositionParamet {
  // 图像的纵横比,如果想要圆形,height/width
  float aspectRatio = 0.5625f;
  // 圆形区域的中心
  vec2f center = {0.5f, 0.5f};
  // 圆形区域的半径
  float radius = 0.25f;

  inline bool operator==(const PositionParamet& right) const {
    return this->aspectRatio == right.aspectRatio &&
           this->center == right.center && this->radius == right.radius;
  }
};

// 圆形区域
struct SelectiveParamet {
  // 图像的纵横比,如果想要圆形,height/width
  float aspectRatio = 0.5625f;
  // 圆形区域的中心
  vec2f center = {0.5f, 0.5f};
  // 圆形区域的半径
  float radius = 0.25f;
  // 圆形区域的大小
  float size = 0.125f;
  inline bool operator==(const SelectiveParamet& right) const {
    return this->aspectRatio == right.aspectRatio &&
           this->center == right.center && this->radius == right.radius &&
           this->size == size;
  }
};

// 圆形区域模糊
struct BlurPositionParamet {
  GaussianBlurParamet gaussian = {};
  PositionParamet blurPosition = {};
};

// 圆形区域不模糊
struct BlurSelectiveParamet {
  GaussianBlurParamet gaussian = {};
  SelectiveParamet blurPosition = {};
};

struct SphereRefractionParamet {
  // 图像的纵横比,如果想要圆形,height/width
  float aspectRatio = 0.5625f;
  vec2f center = {0.5f, 0.5f};
  float radius = 0.25f;
  float refractiveIndex = 0.71f;
  inline bool operator==(const SphereRefractionParamet& right) const {
    return this->aspectRatio == right.aspectRatio &&
           this->center == right.center && this->radius == right.radius &&
           this->refractiveIndex == refractiveIndex;
  }
};

// 对图像或视频应用像素化/半色调效果,如马赛克/新闻打印
struct PixellateParamet {
  // 像素的大小,以图像的宽度和高度的分数为单位(0.0-1.0,默认为0.05)
  float fractionalWidthOfPixel = 0.01f;
  // 图像的纵横比,如果想要圆形,height/width
  float aspectRatio = 0.5625f;
  inline bool operator==(const PixellateParamet& right) const {
    return this->fractionalWidthOfPixel == right.fractionalWidthOfPixel &&
           this->aspectRatio == right.aspectRatio;
  }
};

// 通过将矩阵应用于图像来变换图像的颜色
struct ColorMatrixParamet {
  // 新的变换后的颜色替换每个像素的原始颜色的程度
  float intensity = 1.0f;
  // 用于转换图像中每种颜色的4x4矩阵
  Mat4x4f mat = {};

  inline bool operator==(const ColorMatrixParamet& right) const {
    return this->intensity == right.intensity && this->mat == right.mat;
  }
};

// 裁剪图像的特定区域
struct CropParamet {
  float left = 0.0f;
  float top = 0.0f;
  // width/height只要有一个为0,就会自动使用上一层的大小
  int32_t width = 0;
  int32_t height = 0;
  vec4f fillColor = {0, 0, 0, 0};

  inline bool operator==(const CropParamet& right) const {
    return this->left == right.left && this->top == right.top &&
           this->width == right.width && this->height == right.height &&
           this->fillColor == right.fillColor;
  }
};

// 根据图像的亮度在两种用户指定的颜色之间进行混合
struct FalseColorParamet {
  // 暗区
  vec3f firstColor = {0.0f, 0.0f, 0.5f};
  // 亮区
  vec3f secondColor = {1.0f, 0.0f, 0.0f};
  inline bool operator==(const FalseColorParamet& right) const {
    return this->firstColor == right.firstColor &&
           this->secondColor == right.secondColor;
  }
};

// 使用颜色和强度独立地着色图像的阴影和高光
struct HighlightShadowTintParamet {
  // 阴影着色强度,从0.0到1.0.默认值:0.0.
  float shadowTintIntensity = 0.0f;
  // 阴影色调RGB颜色(GPUVector4).默认值:({1.0f, 0.0f, 0.0f, 1.0f}红色).
  vec3f shadowTintColor = {1.0f, 0.0f, 0.0f};
  // 突出显示色调强度,从0.0到1.0,默认值为0.0.
  float highlightTintIntensity = 1.0f;
  // highlightTintColor:高亮色调RGB颜色(GPUVector4).默认值:({0.0f,
  // 0.0f, 1.0f, 1.0f}蓝色).
  vec3f highlightTintColor = {0.0f, 0.0f, 1.0f};
  inline bool operator==(const HighlightShadowTintParamet& right) const {
    return this->shadowTintIntensity == right.shadowTintIntensity &&
           this->shadowTintColor == right.shadowTintColor &&
           this->highlightTintIntensity == right.highlightTintIntensity &&
           this->highlightTintColor == right.highlightTintColor;
  }
};

// 类似Photoshop的色阶调整,所有参数在[0,1]范围内浮动
struct LevelsParamet {
  vec3f minVec = {0.0, 0.0, 0.0};
  vec3f gammaVec = {1.0, 1.0, 1.0};
  vec3f maxVec = {1.0, 1.0, 1.0};
  vec3f minOut = {0.0, 0.0, 0.0};
  vec3f maxOut = {1.0, 1.0, 1.0};
  inline bool operator==(const LevelsParamet& right) const {
    return this->minVec == right.minVec && this->gammaVec == right.gammaVec &&
           this->maxVec == right.maxVec && this->minOut == right.minOut &&
           this->maxOut == right.maxOut;
  }
};

// 根据每个像素的亮度将图像转换为单色版本
struct MonochromeParamet {
  // 特定颜色替换正常图像颜色的程度(0.0-1.0,默认值为1.0)
  float intensity = 1.0f;
  // 用作效果基础的颜色,默认为(0.6,0.45,0.3,1.0)
  vec3f color = {0.6f, 0.45f, 0.3f};
  inline bool operator==(const MonochromeParamet& right) const {
    return this->intensity == right.intensity && this->color == right.color;
  }
};

// 生成充满Perlin噪点的图像
struct PerlinNoiseParamet {
  // 产生的噪声的标度
  float scale = 8.0f;
  // 噪声颜色最小值
  vec4f colorStart = {0.0, 0.0, 0.0, 1.0};
  // 噪声颜色最大值
  vec4f colorFinish = {1.0, 1.0, 1.0, 1.0};
  inline bool operator==(const PerlinNoiseParamet& right) const {
    return this->colorStart == right.colorStart &&
           this->colorFinish == right.colorFinish && this->scale == right.scale;
  }
};

// 基于极坐标而不是笛卡尔坐标,对图像或视频应用像素化效果
struct PolarPixellateParamet {
  // 要应用像素化的中心
  vec2f center = {0.5f, 0.5f};
  // 像素大小,分为宽度和高度分量.默认值为(0.05,0.05)
  vec2f size = {0.05f, 0.05f};

  inline bool operator==(const PolarPixellateParamet& right) const {
    return this->center == right.center && this->size == right.size;
  }
};

// 在图像上创建漩涡形失真
struct SwirlParamet {
  // 要绕图像扭曲的图像中心(以0-1.0的标准化坐标表示),默认值为(0.5,0.5)
  vec2f center = {0.5f, 0.5f};
  // 从中心开始应用变形的半径,默认值为0.5
  float radius = 0.5f;
  // 应用于图像的扭曲量,默认值为1.0
  float angle = 1.0f;
  inline bool operator==(const SwirlParamet& right) const {
    return this->center == right.center && this->radius == right.radius &&
           this->angle == right.angle;
  }
};

// 执行渐晕效果,使边缘的图像淡化
struct VignetteParamet {
  // 小插图的中心,以tex坐标(CGPoint)为单位,默认值为(0.5,0.5)
  vec2f vignetteCenter = {0.5f, 0.5f};
  // 用于小插图(GPUVector3)的颜色,默认为黑色
  vec3f vignetteColor = {0.0f, 0.0f, 0.0f};
  // 距小插图效果开始的中心的标准化距离,默认值为0.5
  float vignetteStart = 0.3f;
  // 距小插图效果结束的中心的标准化距离,默认值为0.75
  float vignetteEnd = 0.75f;

  inline bool operator==(const VignetteParamet& right) const {
    return this->vignetteCenter == right.vignetteCenter &&
           this->vignetteColor == right.vignetteColor &&
           this->vignetteStart == right.vignetteStart &&
           this->vignetteEnd == right.vignetteEnd;
  }
};

// 将定向运动模糊应用于图像
struct ZoomBlurParamet {
  // 模糊中心.默认为(0.5,0.5)
  vec2f blurCenter = {0.5f, 0.5f};
  // 模糊大小的倍数,范围从0.0开始,默认为1.0
  float blurSize = 1.0f;
  inline bool operator==(const ZoomBlurParamet& right) const {
    return this->blurCenter == right.blurCenter &&
           this->blurSize == right.blurSize;
  }
};

struct PointsParamet {
  int32_t showCount = 0;
  int32_t radius = 1;
  vec4f color = {1.0f, 0.0, 0.0, 1.0f};
  inline bool operator==(const PointsParamet& right) const {
    return showCount == right.showCount && color == right.color &&
           radius == right.radius;
  }
};

struct DrawRectParamet {
  int32_t radius = 1;
  vec4f rect = {0.0f, 1.0f, 0.0f, 1.0f};
  vec4f color = {1.0f, 0.0, 0.0, 1.0f};
  inline bool operator==(const DrawRectParamet& right) const {
    return rect == right.rect && color == right.color && radius == right.radius;
  }
};


typedef IParamet<SoftEleganceParamet> ASoftEleganceLayer;
typedef IParamet<PerlinNoiseParamet> APerlinNoiseLayer;
typedef IParamet<float> AFloatLayer;

typedef IParamet<vec2f> IStretchDistortionLayer;
typedef IParamet<vec3f> IRGBLayer;
typedef IParamet<uint32_t> IMedianLayer;
typedef IParamet<Mat3x3f> I3x3ConvolutionLayer;
typedef IParamet<int32_t> IMorphLayer;

typedef IParamet<float> IBrightnessLayer;
typedef IParamet<float> IExposureLayer;
typedef IParamet<float> IContrastLayer;
typedef IParamet<float> ISaturationLayer;
typedef IParamet<float> IGammaLayer;
typedef IParamet<float> ISolarizeLayer;
typedef IParamet<float> IHueLayer;
typedef IParamet<float> IVibranceLayer;
typedef IParamet<float> ISepiaLayer;
typedef IParamet<float> IOpacityLayer;
typedef IParamet<float> ILuminanceThresholdLayer;
typedef IParamet<float> IAverageLuminanceThresholdLayer;

typedef IParamet<SizeScaleParamet> ISizeScaleLayer;
typedef IParamet<KernelSizeParamet> IKernelSizeLayer;
typedef IParamet<GaussianBlurParamet> IGaussianBlurLayer;
typedef IParamet<ChromaKeyParamet> IChromaKeyLayer;
typedef IParamet<AdaptiveThresholdParamet> IAdaptiveThresholdLayer;
typedef IParamet<GuidedParamet> IGuidedLayer;
typedef IParamet<HarrisCornerDetectionParamet> IHarrisCornerDetectionLayer;
typedef IParamet<NobleCornerDetectionParamet> INobleCornerDetectionLayer;
typedef IParamet<CannyEdgeDetectionParamet> ICannyEdgeDetectionLayer;
typedef IParamet<FASTFeatureParamet> IFASTFeatureLayer;
typedef IParamet<BilateralParamet> IBilateralLayer;
typedef IParamet<DistortionParamet> IDistortionLayer;
typedef IParamet<PositionParamet> IPositionLayer;
typedef IParamet<SelectiveParamet> ISelectiveLayer;
typedef IParamet<BlurPositionParamet> IBlurPositionLayer;
typedef IParamet<BlurSelectiveParamet> IBlurSelectiveLayer;
typedef IParamet<SphereRefractionParamet> ISphereRefractionLayer;
typedef IParamet<PixellateParamet> IPixellateLayer;
typedef IParamet<ColorMatrixParamet> IColorMatrixLayer;
typedef IParamet<CropParamet> ICropLayer;
typedef IParamet<CrosshatchParamet> ICrosshatchLayer;
typedef IParamet<FalseColorParamet> IFalseColorLayer;
typedef IParamet<HazeParamet> IHazeLayer;
typedef IParamet<HighlightShadowParamet> IHighlightShadowLayer;
typedef IParamet<HighlightShadowTintParamet> IHighlightShadowTintLayer;
typedef IParamet<IOSBlurParamet> IIOSBlurLayer;
typedef IParamet<LevelsParamet> ILevelsLayer;
typedef IParamet<MonochromeParamet> IMonochromeLayer;
typedef IParamet<MotionBlurParamet> IMotionBlurLayer;
typedef IParamet<PoissonParamet> IPoissonLayer;
typedef IParamet<PolarPixellateParamet> IPolarPixellateLayer;
typedef IParamet<PolkaDotParamet> IPolkaDotLayer;
typedef IParamet<SharpenParamet> ISharpenLayer;
typedef IParamet<SkinToneParamet> ISkinToneLayer;
typedef IParamet<ToonParamet> IToonLayer;
typedef IParamet<SmoothToonParamet> ISmoothToonLayer;
typedef IParamet<SwirlParamet> ISwirlParametLayer;
typedef IParamet<SharpenParamet> ISharpenParametLayer;
typedef IParamet<ThresholdSobelParamet> IThresholdSobelLayer;
typedef IParamet<TiltShiftParamet> ITiltShiftLayer;
typedef IParamet<UnsharpMaskParamet> IUnsharpMaskLayer;
typedef IParamet<VignetteParamet> IVignetteLayer;
typedef IParamet<WhiteBalanceParamet> IWhiteBalanceLayer;
typedef IParamet<ZoomBlurParamet> IZoomBlurLayer;

typedef IParamet<DrawRectParamet> IDrawRectLayer;

class ILookupLayer {
 public:
  virtual ~ILookupLayer() = default;
  virtual void loadLookUp(uint8_t* data, int32_t size) = 0;
  virtual IVInputLayer* getLookUpInputLayer() = 0;
};

class ISoftEleganceLayer : public IParamet<SoftEleganceParamet> {
 public:
  virtual ~ISoftEleganceLayer() = default;

 public:
  virtual void loadLookUp1(uint8_t* data, int32_t size) = 0;
  virtual void loadLookUp2(uint8_t* data, int32_t size) = 0;

  virtual IVInputLayer* getLookUpInputLayer1() = 0;
  virtual IVInputLayer* getLookUpInputLayer2() = 0;
};

class IHSBLayer {
 public:
  virtual ~IHSBLayer() = default;

 public:
  // 重置过滤器以使其不具有任何变换.
  virtual void reset() = 0;
  // 向滤镜添加色相旋转.
  // 色相旋转范围为[-360,360],其中0为不变.
  // 请注意,此调整是累加的,因此如有必要,请使用重置方法.
  virtual void rotateHue(const float& h) = 0;
  // 向滤镜添加饱和度调整.
  // 饱和度调整在[0.0,2.0]的范围内,其中1.0为不变.
  // 请注意,此调整是累加的,因此如有必要,请使用重置方法.
  virtual void adjustSaturation(const float& h) = 0;
  // 向滤镜添加亮度调整
  // 亮度调整在[0.0,2.0]的范围内,其中1.0不变.
  // 请注意,此调整是累加的,因此如有必要,请使用重置方法.
  virtual void adjustBrightness(const float& h) = 0;
};

class IMotionDetectorObserver {
 public:
  virtual ~IMotionDetectorObserver() = default;
  virtual void onMotion(const vec4f& vec) = 0;
};

class IMotionDetectorLayer : public IParamet<float> {
 public:
  virtual ~IMotionDetectorLayer() = default;

 public:
  virtual void setObserver(IMotionDetectorObserver* observer) = 0;
};

class IPerlinNoiseLayer : public IParamet<PerlinNoiseParamet> {
 public:
  virtual ~IPerlinNoiseLayer() = default;

 public:
  virtual void setImageSize(int32_t width, int32_t height) = 0;
};

class IDrawPointsLayer : public IParamet<PointsParamet> {
 public:
  IDrawPointsLayer() = default;
  virtual ~IDrawPointsLayer() = default;

 public:
  virtual void drawPoints(const vec2f* points, int32_t size, vec4f color,
                          int32_t raduis) = 0;
};

class IUVMapLayer {
 public:
  virtual ~IUVMapLayer() = default;

 public:
  virtual void updateMap(IImageBuffer* xMap, IImageBuffer* yMap) = 0;
};

extern "C" {

AVOX_EXPORT IPipeGraph* createVkPipeGraph();

// AVOX_EXPORT ITVInputLayer* getVkInputLayer(IPipeGraph* graph);

// AVOX_EXPORT ITVOutputLayer* getVkOutputLayer(IPipeGraph* graph);

// AVOX_EXPORT ITYUVLayer* getVkYUV2RGBALayer(IPipeGraph* graph);
}

}