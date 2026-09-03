#include "TiledInpainter.hpp"
#include "LamaInpainter.hpp"
#include "AOTGanInpainter.hpp"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace avox {

// ============== TiledInpainter ==============

TiledInpainter::TiledInpainter() = default;

TiledInpainter::~TiledInpainter() = default;

void TiledInpainter::setInpainter(LamaInpainter* inpainter_) {
  lamaInpainter = inpainter_;
}

void TiledInpainter::setInpainter(AOTGanInpainter* inpainter_) {
  aotganInpainter = inpainter_;
}

int TiledInpainter::getNumTilesX(int width) const {
  if (width <= tileSize) return 1;
  // numTilesX = ceil((W - overlap) / (tileSize - overlap))
  int step = tileSize - overlap;
  return static_cast<int>(std::ceil(static_cast<float>(width - overlap) / step));
}

int TiledInpainter::getNumTilesY(int height) const {
  if (height <= tileSize) return 1;
  // numTilesY = ceil((H - overlap) / (tileSize - overlap))
  int step = tileSize - overlap;
  return static_cast<int>(std::ceil(static_cast<float>(height - overlap) / step));
}

std::vector<TileInfo> TiledInpainter::computeTiles(int width, int height) const {
  std::vector<TileInfo> tiles;

  int numTilesX = getNumTilesX(width);
  int numTilesY = getNumTilesY(height);

  int step = tileSize - overlap;

  for (int iy = 0; iy < numTilesY; ++iy) {
    for (int ix = 0; ix < numTilesX; ++ix) {
      TileInfo tile;

      // 计算源图像中的坐标
      // tileX = clamp(x * (tileSize - overlap), 0, W - tileSize)
      tile.srcX = std::min(ix * step, std::max(0, width - tileSize));
      tile.srcY = std::min(iy * step, std::max(0, height - tileSize));

      // 计算实际分块大小 (边界可能小于 tileSize)
      tile.tileWidth = std::min(tileSize, width - tile.srcX);
      tile.tileHeight = std::min(tileSize, height - tile.srcY);

      tile.indexX = ix;
      tile.indexY = iy;

      tiles.push_back(tile);
    }
  }

  return tiles;
}

void TiledInpainter::extractTile(const uint8_t* srcImage,
                                  const uint8_t* srcMask,
                                  int srcWidth, int srcHeight,
                                  const TileInfo& tile,
                                  uint8_t* tileImage,
                                  uint8_t* tileMask) {
  // 提取 RGB 图像分块
  for (int y = 0; y < tile.tileHeight; ++y) {
    int srcY = tile.srcY + y;
    if (srcY >= srcHeight) break;

    const uint8_t* srcRow = srcImage + (srcY * srcWidth * 3) + (tile.srcX * 3);
    uint8_t* dstRow = tileImage + (y * tile.tileWidth * 3);
    std::memcpy(dstRow, srcRow, tile.tileWidth * 3);
  }

  // 提取 Mask 分块
  for (int y = 0; y < tile.tileHeight; ++y) {
    int srcY = tile.srcY + y;
    if (srcY >= srcHeight) break;

    const uint8_t* srcMaskRow = srcMask + (srcY * srcWidth) + tile.srcX;
    uint8_t* dstMaskRow = tileMask + (y * tile.tileWidth);
    std::memcpy(dstMaskRow, srcMaskRow, tile.tileWidth);
  }
}

void TiledInpainter::computeBlendWeights(int tileWidth, int tileHeight,
                                          float* weights) const {
  // 计算线性权重
  // weight = min(1.0, min(x, tileSize-x) / overlap)
  for (int y = 0; y < tileHeight; ++y) {
    for (int x = 0; x < tileWidth; ++x) {
      float wx = 1.0f;
      float wy = 1.0f;

      // X 方向权重
      if (x < overlap) {
        wx = static_cast<float>(x) / overlap;
      } else if (x >= tileWidth - overlap) {
        wx = static_cast<float>(tileWidth - 1 - x) / overlap;
      }

      // Y 方向权重
      if (y < overlap) {
        wy = static_cast<float>(y) / overlap;
      } else if (y >= tileHeight - overlap) {
        wy = static_cast<float>(tileHeight - 1 - y) / overlap;
      }

      // 组合权重
      weights[y * tileWidth + x] = wx * wy;
    }
  }
}

void TiledInpainter::blendTile(const uint8_t* tileOutput,
                                const TileInfo& tile,
                                int srcWidth, int srcHeight,
                                float* accumulator,
                                float* weightSum) {
  // 使用局部 weight buffer，避免修改成员变量
  std::vector<float> tileWeight(tile.tileWidth * tile.tileHeight);
  computeBlendWeights(tile.tileWidth, tile.tileHeight, tileWeight.data());

  // 融合到累加器
  for (int y = 0; y < tile.tileHeight; ++y) {
    int dstY = tile.srcY + y;
    if (dstY >= srcHeight) break;

    for (int x = 0; x < tile.tileWidth; ++x) {
      int dstX = tile.srcX + x;
      if (dstX >= srcWidth) break;

      int dstIdx = dstY * srcWidth + dstX;
      int tileIdx = y * tile.tileWidth + x;

      float w = tileWeight[tileIdx];

      // 对每个通道累加
      for (int c = 0; c < 3; ++c) {
        accumulator[dstIdx * 3 + c] += tileOutput[tileIdx * 3 + c] * w;
      }
      weightSum[dstIdx] += w;
    }
  }
}

bool TiledInpainter::inpaint(
    const uint8_t* rgbImage,
    const uint8_t* mask,
    int width, int height,
    uint8_t* output,
    std::function<bool(const uint8_t*, const uint8_t*, int, int, uint8_t*)> inpainterFn) {

  // 检查是否需要分块
  if (width <= tileSize && height <= tileSize) {
    // 不需要分块，直接处理
    return inpainterFn(rgbImage, mask, width, height, output);
  }

  // 计算分块
  auto tiles = computeTiles(width, height);
  if (tiles.empty()) {
    return false;
  }

  // 初始化累加缓冲区
  size_t imageSize = static_cast<size_t>(width) * height * 3;
  size_t maskSize = static_cast<size_t>(width) * height;

  accumulatorBuffer.assign(imageSize, 0.0f);
  weightBuffer.assign(maskSize, 0.0f);

  // 处理每个分块
  for (const auto& tile : tiles) {
    // 分配分块缓冲区
    size_t tileSize = static_cast<size_t>(tile.tileWidth) * tile.tileHeight;
    tileImageBuffer.resize(tileSize * 3);
    tileMaskBuffer.resize(tileSize);
    tileOutputBuffer.resize(tileSize * 3);

    // 提取分块数据
    extractTile(rgbImage, mask, width, height, tile,
                tileImageBuffer.data(), tileMaskBuffer.data());

    // 检查分块是否有需要修复的区域
    bool hasMask = false;
    for (size_t i = 0; i < tileSize; ++i) {
      if (tileMaskBuffer[i] > 0) {
        hasMask = true;
        break;
      }
    }

    if (!hasMask) {
      // 没有需要修复的区域，直接使用原图
      blendTile(tileImageBuffer.data(), tile, width, height,
                accumulatorBuffer.data(), weightBuffer.data());
      continue;
    }

    // 执行修复
    bool success = inpainterFn(
        tileImageBuffer.data(),
        tileMaskBuffer.data(),
        tile.tileWidth, tile.tileHeight,
        tileOutputBuffer.data());

    if (!success) {
      // 修复失败，使用原图
      blendTile(tileImageBuffer.data(), tile, width, height,
                accumulatorBuffer.data(), weightBuffer.data());
      continue;
    }

    // 融合结果
    blendTile(tileOutputBuffer.data(), tile, width, height,
              accumulatorBuffer.data(), weightBuffer.data());
  }

  // 归一化输出
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int idx = y * width + x;
      float w = weightBuffer[idx];

      if (w > 0.0f) {
        for (int c = 0; c < 3; ++c) {
          float val = accumulatorBuffer[idx * 3 + c] / w;
          output[idx * 3 + c] = static_cast<uint8_t>(
              std::clamp(static_cast<int>(std::round(val)), 0, 255));
        }
      } else {
        // 没有权重的区域，复制原图
        for (int c = 0; c < 3; ++c) {
          output[idx * 3 + c] = rgbImage[idx * 3 + c];
        }
      }
    }
  }

  return true;
}

bool TiledInpainter::inpaintWithLama(const uint8_t* rgbImage,
                                      const uint8_t* mask,
                                      int width, int height,
                                      uint8_t* output) {
  if (!lamaInpainter) {
    return false;
  }

  auto lamaFn = [this](const uint8_t* tileImage, const uint8_t* tileMask,
                       int tileWidth, int tileHeight, uint8_t* tileOutput) -> bool {
    return lamaInpainter->inpaint(tileImage, tileMask, tileWidth, tileHeight, tileOutput);
  };

  return inpaint(rgbImage, mask, width, height, output, lamaFn);
}

bool TiledInpainter::inpaintWithAOTGan(const uint8_t* rgbImage,
                                        const uint8_t* mask,
                                        int width, int height,
                                        uint8_t* output) {
  if (!aotganInpainter) {
    return false;
  }

  auto aotganFn = [this](const uint8_t* tileImage, const uint8_t* tileMask,
                         int tileWidth, int tileHeight, uint8_t* tileOutput) -> bool {
    return aotganInpainter->inpaint(tileImage, tileMask, tileWidth, tileHeight, tileOutput);
  };

  return inpaint(rgbImage, mask, width, height, output, aotganFn);
}

}
