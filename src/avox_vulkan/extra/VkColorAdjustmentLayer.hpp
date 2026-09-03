#pragma once

#include <memory>

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"

namespace avox {

// 基础图像调整: 色调/亮度/对比度/饱和度/伽玛 合并到单 shader 单 pass
// hue 的度->弧度转换在 shader 内做, 这里直接用宏上传 paramet
class VkBasicAdjustLayer : public VkLayer, public IParamet<BasicAdjustParamet> {
  AVOX_LAYER_GETNAME(VkBasicAdjustLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkBasicAdjustLayer(/* args */);
  virtual ~VkBasicAdjustLayer();
};

class VkBrightnessLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkBrightnessLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkBrightnessLayer(/* args */);
  virtual ~VkBrightnessLayer();
};

// Exposure ranges from -10.0 to 10.0, with 0.0 as the normal level
class VkExposureLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkExposureLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkExposureLayer(/* args */);
  virtual ~VkExposureLayer();
};

// Gamma ranges from 0.0 to 3.0, with 1.0 as the normal level
class VkGammaLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkGammaLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkGammaLayer(/* args */);
  virtual ~VkGammaLayer();
};

// 去雾
class VkHazeLayer : public VkLayer, public IParamet<HazeParamet> {
  AVOX_LAYER_GETNAME(VkHazeLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkHazeLayer(/* args */);
  virtual ~VkHazeLayer();
};

// 调整图像的阴影和高光
class VKHighlightShadowLayer : public VkLayer,
                               public IParamet<HighlightShadowParamet> {
  AVOX_LAYER_GETNAME(VKHighlightShadowLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VKHighlightShadowLayer(/* args */);
  virtual ~VKHighlightShadowLayer();
};

// 允许您使用颜色和强度独立地着色图像的阴影和高光
class VKHighlightShadowTintLayer : public VkLayer,
                                   public IParamet<HighlightShadowTintParamet> {
  AVOX_LAYER_GETNAME(VKHighlightShadowTintLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VKHighlightShadowTintLayer(/* args */);
  virtual ~VKHighlightShadowTintLayer();
};

// Saturation ranges from 0.0 (fully desaturated) to 2.0 (max saturation),
// with 1.0 as the normal level
class VkSaturationLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkSaturationLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkSaturationLayer(/* args */);
  virtual ~VkSaturationLayer();
};

class VkMonochromeLayer : public VkLayer, public IParamet<MonochromeParamet> {
  AVOX_LAYER_GETNAME(VkMonochromeLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkMonochromeLayer(/* args */);
  virtual ~VkMonochromeLayer();
};

class VkOpacityLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkOpacityLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkOpacityLayer(/* args */);
  virtual ~VkOpacityLayer();
};

class VkRGBLayer : public VkLayer, public IParamet<vec3f> {
  AVOX_LAYER_GETNAME(VkRGBLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkRGBLayer(/* args */);
  ~VkRGBLayer();
};

class VkSkinToneLayer : public VkLayer, public IParamet<SkinToneParamet> {
  AVOX_LAYER_GETNAME(VkSkinToneLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkSkinToneLayer(/* args */);
  ~VkSkinToneLayer();
};

class VkSolarizeLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkSolarizeLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkSolarizeLayer(/* args */);
  ~VkSolarizeLayer();
};

// Modifies the saturation of desaturated colors, leaving saturated colors
// unmodified. Value -1 to 1 (-1 is minimum vibrance, 0 is no change, and 1 is
// maximum vibrance)
class VkVibranceLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkVibranceLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkVibranceLayer(/* args */);
  ~VkVibranceLayer();
};

class VkWhiteBalanceLayer : public VkLayer,
                            public IParamet<WhiteBalanceParamet> {
  AVOX_LAYER_GETNAME(VkWhiteBalanceLayer)
 private:
  /* data */
 public:
  VkWhiteBalanceLayer(/* args */);
  ~VkWhiteBalanceLayer();

 private:
  void parametTransform();

 protected:
  virtual void onUpdateParamet() override;
};

class VkChromaKeyLayer : public VkLayer, public IParamet<ChromaKeyParamet> {
  AVOX_LAYER_GETNAME(VkChromaKeyLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 protected:
 public:
  VkChromaKeyLayer(/* args */);
  virtual ~VkChromaKeyLayer();
};

class VkColorInvertLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkColorInvertLayer)
 private:
  /* data */
 public:
  VkColorInvertLayer(/* args */);
  virtual ~VkColorInvertLayer();
};

class VkContrastLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkContrastLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkContrastLayer(/* args */);
  virtual ~VkContrastLayer();
};

class VkFalseColorLayer : public VkLayer, public IParamet<FalseColorParamet> {
  AVOX_LAYER_GETNAME(VkFalseColorLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkFalseColorLayer(/* args */);
  virtual ~VkFalseColorLayer();
};

class VkHueLayer : public VkLayer, public IParamet<float> {
  AVOX_LAYER_GETNAME(VkHueLayer)

 private:
  /* data */
 public:
  VkHueLayer(/* args */);
  virtual ~VkHueLayer();

 private:
  void transformParamet();

 protected:
  virtual void onUpdateParamet() override;
};

class VkLevelsLayer : public VkLayer, public IParamet<LevelsParamet> {
  AVOX_LAYER_GETNAME(VkLevelsLayer)
  AVOX_VULKAN_PARAMETUPDATE()
 private:
  /* data */
 public:
  VkLevelsLayer(/* args */);
  virtual ~VkLevelsLayer();
};

}