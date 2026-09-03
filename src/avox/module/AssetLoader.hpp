#pragma once

#include "../AvoxDef.h"

#include <string>
#include <vector>

namespace avox {

// 统一资源加载器 - 管理多平台资源加载 (Shader/Font/Image/Model)
class AVOX_EXPORT AssetLoader {
 public:
  // 检查是否是 assets 路径 (相对路径，不是绝对路径)
  static bool isAssetPath(const std::string& path);

  // 通用入口: 从 assets 或文件加载到内存
  // relativePath: 相对路径，如 "models/lama.onnx", "fonts/simhei.ttf"
  // 返回: 加载的数据，失败返回空
  static std::vector<uint8_t> loadToMemory(const char* relativePath);

  // 获取错误信息
  static const char* getLastError();

  // 保存数据到文件
  // relativePath: 相对路径，如 "config/translation.json"
  // Windows/Linux: 双写到系统目录和 assets/ 目录
  // Android/iOS: 写入到可写目录
  // 返回: 成功返回 true
  static bool saveToFile(const char* relativePath, const std::vector<uint8_t>& data);

  // 仅从 assets 加载, 不查系统目录 (链模板等需固定来源的场景)
  static std::vector<uint8_t> loadFromAssets(const char* relativePath);

  // 仅保存到 assets/ 目录, 不写系统目录 (与 loadFromAssets 配对)
  static bool saveToAssets(const char* relativePath, const std::vector<uint8_t>& data);

  // 获取系统配置目录路径
  // Windows: %LOCALAPPDATA%/avox/
  // Linux: ~/.local/share/avox/
  // Android: cacheDir/
  // iOS: Documents/
  static std::string getSystemConfigPath();

#ifdef __ANDROID__
  // 复制 assets 文件到缓存目录，返回真实文件路径
  // assetPath: assets 相对路径，如 "models/stt/zh-en/model.onnx"
  // cacheSubDir: 缓存子目录名，如 "sherpa_models"
  // 返回: 缓存目录中的真实文件路径，失败返回空
  // 首次调用会复制，后续调用直接返回已存在的文件
  static std::string copyAssetToCache(const char* assetPath,
                                       const char* cacheSubDir);

  // 复制 assets 目录到缓存目录，返回缓存目录路径
  // assetDirPath: assets 目录路径，如 "models/translation/opus-mt-ja-zh"
  // cacheSubDir: 缓存子目录名，如 "translation_models"
  // 返回: 缓存目录路径，失败返回空
  // 首次调用会递归复制所有文件，后续调用检测到目录存在则跳过
  static std::string copyDirectoryToCache(const char* assetDirPath,
                                           const char* cacheSubDir);

  // 获取应用缓存目录
  // 返回: 缓存目录路径
  static std::string getCacheDir();
#endif

 private:
  // Android assets 加载
#ifdef __ANDROID__
  static std::vector<uint8_t> loadFromAndroidAssets(const char* assetPath);
  // 递归复制目录
  static bool copyAssetDirRecursive(const std::string& assetDir,
                                     const std::string& targetDir);
#endif

#ifdef __APPLE__
  // iOS Bundle 加载
  static std::vector<uint8_t> loadFromIOSBundle(const char* bundlePath);
#endif

  // 从文件系统加载 (非 assets)
  static std::vector<uint8_t> loadFromFile(const std::string& fullPath);
};

}
