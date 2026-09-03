#pragma once

#include "../core/InpaintTypes.hpp"
#include "../core/InpaintConfig.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace avox {

// Forward declaration
class LamaInpainter;
class AOTGanInpainter;

// ============== 分块信息 ==============

struct TileInfo {
  int srcX = 0;          // 在源图像中的 x 坐标
  int srcY = 0;          // 在源图像中的 y 坐标
  int tileWidth = 0;     // 分块宽度
  int tileHeight = 0;    // 分块高度
  int indexX = 0;        // x 方向索引
  int indexY = 0;        // y 方向索引
};

// ============== 分块修复器 ==============

class TiledInpainter {
 public:
  TiledInpainter();
  ~TiledInpainter();

  // 设置底层修复器 (不持有所有权)
  void setInpainter(LamaInpainter* inpainter);

  // 设置 AOT-GAN 修复器 (不持有所有权)
  void setInpainter(AOTGanInpainter* inpainter);

  // 设置分块参数
  void setTileSize(int size) { tileSize = size; }
  void setOverlap(int overlap_) { overlap = overlap_; }

  // 分块修复
  // inpainterFn: 实际执行修复的回调函数
  //   bool inpainterFn(const uint8_t* tileImage, const uint8_t* tileMask,
  //                    int tileWidth, int tileHeight, uint8_t* tileOutput)
  bool inpaint(const uint8_t* rgbImage,
               const uint8_t* mask,
               int width, int height,
               uint8_t* output,
               std::function<bool(const uint8_t*, const uint8_t*, int, int, uint8_t*)> inpainterFn);

  // 使用内部 LamaInpainter 进行修复
  bool inpaintWithLama(const uint8_t* rgbImage,
                       const uint8_t* mask,
                       int width, int height,
                       uint8_t* output);

  // 使用内部 AOT-GAN 进行修复
  bool inpaintWithAOTGan(const uint8_t* rgbImage,
                         const uint8_t* mask,
                         int width, int height,
                         uint8_t* output);

  // 计算分块信息
  std::vector<TileInfo> computeTiles(int width, int height) const;

  // 获取分块数量
  int getNumTilesX(int width) const;
  int getNumTilesY(int height) const;

 private:
  LamaInpainter* lamaInpainter = nullptr;      // LaMa 修复器
  AOTGanInpainter* aotganInpainter = nullptr;   // AOT-GAN 修复器
  int tileSize = 512;
  int overlap = 32;

  // 缓冲区
  std::vector<uint8_t> tileImageBuffer;
  std::vector<uint8_t> tileMaskBuffer;
  std::vector<uint8_t> tileOutputBuffer;
  std::vector<float> weightBuffer;
  std::vector<float> accumulatorBuffer;

  // 提取分块图像和 mask
  void extractTile(const uint8_t* srcImage,
                   const uint8_t* srcMask,
                   int srcWidth, int srcHeight,
                   const TileInfo& tile,
                   uint8_t* tileImage,
                   uint8_t* tileMask);

  // 融合分块结果到输出
  void blendTile(const uint8_t* tileOutput,
                 const TileInfo& tile,
                 int srcWidth, int srcHeight,
                 float* accumulator,
                 float* weightSum);

  // 计算融合权重
  void computeBlendWeights(int tileWidth, int tileHeight,
                           float* weights) const;
};

}
