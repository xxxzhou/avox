#pragma once

#include "../VkTemplate.hpp"
#include "../layer/VkLayer.hpp"
#include "avox/AvoxLayer.h"

namespace avox {

// Anime4K CNN first conv layer (3ch -> 4ch, rgba32f output)
class VkAnime4KConv0Layer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KConv0Layer)

 public:
  VkAnime4KConv0Layer(const std::string& shaderPath);
  virtual ~VkAnime4KConv0Layer();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K CNN middle conv layer (8ch -> 4ch with ReLU split, rgba32f)
class VkAnime4KConvLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KConvLayer)

 public:
  VkAnime4KConvLayer(const std::string& shaderPath);
  virtual ~VkAnime4KConvLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K Restore output layer (MAIN + 7 conv textures -> rgba8)
class VkAnime4KRestoreOutputLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KRestoreOutputLayer)

 public:
  VkAnime4KRestoreOutputLayer(const std::string& shaderPath);
  virtual ~VkAnime4KRestoreOutputLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K Upscale output layer (7 conv textures -> rgba32f, no MAIN)
class VkAnime4KUpscaleOutputLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KUpscaleOutputLayer)

 public:
  VkAnime4KUpscaleOutputLayer(const std::string& shaderPath);
  virtual ~VkAnime4KUpscaleOutputLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K Depth-to-Space layer (MAIN + convLast -> 2x rgba8)
class VkAnime4KD2SLayer : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KD2SLayer)

 public:
  VkAnime4KD2SLayer(const std::string& shaderPath);
  virtual ~VkAnime4KD2SLayer();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitLayer() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K Clamp Highlights horizontal max
class VkAnime4KClampHPass : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KClampHPass)

 public:
  VkAnime4KClampHPass(const std::string& shaderPath);
  virtual ~VkAnime4KClampHPass();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K Clamp Highlights vertical max
class VkAnime4KClampVPass : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KClampVPass)

 public:
  VkAnime4KClampVPass(const std::string& shaderPath);
  virtual ~VkAnime4KClampVPass();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K Clamp Highlights apply
class VkAnime4KClampApplyPass : public VkLayer {
  AVOX_LAYER_GETNAME(VkAnime4KClampApplyPass)

 public:
  VkAnime4KClampApplyPass(const std::string& shaderPath);
  virtual ~VkAnime4KClampApplyPass();

 protected:
  virtual bool getSampled(int32_t inIndex) override { return true; }
  virtual void onInitGraph() override;
  virtual void onInitVkBuffer() override;
};

// Anime4K composite layer (VkGroupLayer)
// Orchestrates Restore CNN, Upscale CNN, and Clamp Highlights
class VkAnime4KLayer : public VkGroupLayer, public IParamet<Anime4KParamet> {
  AVOX_LAYER_GETNAME(VkAnime4KLayer)

 private:
  // Restore CNN sub-layers
  VKTNodePtr<VkAnime4KConv0Layer> restoreConv0Layer = nullptr;
  std::vector<VKTNodePtr<VkAnime4KConvLayer>> restoreConvLayers;
  VKTNodePtr<VkAnime4KRestoreOutputLayer> restoreOutputLayer = nullptr;
  // Upscale CNN sub-layers
  VKTNodePtr<VkAnime4KConv0Layer> upscaleConv0Layer = nullptr;
  std::vector<VKTNodePtr<VkAnime4KConvLayer>> upscaleConvLayers;
  VKTNodePtr<VkAnime4KUpscaleOutputLayer> upscaleOutputLayer = nullptr;
  VKTNodePtr<VkAnime4KD2SLayer> d2sLayer = nullptr;
  // Clamp Highlights sub-layers
  VKTNodePtr<VkAnime4KClampHPass> clampHPass = nullptr;
  VKTNodePtr<VkAnime4KClampVPass> clampVPass = nullptr;
  VKTNodePtr<VkAnime4KClampApplyPass> clampApplyPass = nullptr;

 public:
  VkAnime4KLayer();
  virtual ~VkAnime4KLayer();

 protected:
  virtual void onUpdateParamet() override;
  virtual void onInitGroup() override;
  virtual void onInitNode() override;
  virtual void onInitLayer() override;
};

}