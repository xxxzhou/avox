/**
 * @file inpainttest.cpp
 * @brief 水印去除测试程序 (IWatermarkRemoval 纯接口)
 *
 * 功能:
 * 1. 从命令行或 data/images 加载测试图片
 * 2. 使用 IWatermarkRemoval 接口进行水印检测和去除
 * 3. 保存结果图片
 * 4. 显示检测信息和性能数据
 *
 * 用法:
 *   inpainttest                    # 使用默认参数处理测试图片
 *   inpainttest -i <image>         # 指定输入图片
 *   inpainttest -i <image> -o <output>  # 指定输出路径
 *   inpainttest -l 2               # 使用 high 模式
 *   inpainttest -g                 # 使用 GPU
 *   inpainttest -r "100,200,50,30" # 手动指定水印区域
 *   inpainttest -d <directory>     # 批量处理目录
 *   inpainttest -h                 # 显示帮助
 */

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <filesystem>
#include <memory>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <array>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/AvoxVideo.h"
#include "avox/Avox.hpp"

using namespace avox;

namespace fs = std::filesystem;

// 命令行参数结构
struct Options {
  std::string inputPath;
  std::string outputPath;
  std::string directory;
  std::string regions;
  std::string maskPath;    // 外部 mask 文件路径
  int modelLevel = 1;      // 0=mini, 1=base, 2=high
  int modelType = 0;       // 0=lama, 1=aotgan
  int useGPU = 0;          // 0=CPU, 1=GPU
  bool detectOnly = false; // 只检测不修复
  bool verbose = false;
  bool cleanMode = false;
  bool cleanYes = false;
};

// 打印帮助信息
void printHelp(const char* programName) {
  std::cout << "用法: " << programName << " [选项]\n\n"
            << "选项:\n"
            << "  -i, --input <path>    输入图片路径\n"
            << "  -o, --output <path>   输出图片路径 (默认: 自动生成)\n"
            << "  -d, --dir <path>      批量处理目录中的所有图片\n"
            << "  -r, --regions <reg>   手动指定水印区域 (格式: x1,y1,w1,h1;x2,y2,w2,h2)\n"
            << "  -k, --mask <path>     使用外部 mask 文件进行修复 (跳过检测)\n"
            << "  -m, --detect-only     只检测不修复，保存 mask 图片\n"
            << "  -l, --level <level>   模型级别 (0=mini, 1=base, 2=high, 默认: 1)\n"
            << "  -t, --type <type>     修复模型类型 (0=lama, 1=aotgan, 默认: 0)\n"
            << "  -g, --gpu             使用 GPU (默认: CPU)\n"
            << "  -v, --verbose         显示详细输出\n"
            << "  -c, --clean           清理生成的文件\n"
            << "  -y, --yes             清理时直接确认删除\n"
            << "  -h, --help            显示帮助信息\n"
            << std::endl;
}

// 解析命令行参数
bool parseArgs(int argc, char* argv[], Options& opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") { printHelp(argv[0]); return false; }
    if (arg == "-i" || arg == "--input") { if (i + 1 < argc) opts.inputPath = argv[++i]; continue; }
    if (arg == "-o" || arg == "--output") { if (i + 1 < argc) opts.outputPath = argv[++i]; continue; }
    if (arg == "-d" || arg == "--dir") { if (i + 1 < argc) opts.directory = argv[++i]; continue; }
    if (arg == "-r" || arg == "--regions") { if (i + 1 < argc) opts.regions = argv[++i]; continue; }
    if (arg == "-k" || arg == "--mask") { if (i + 1 < argc) opts.maskPath = argv[++i]; continue; }
    if (arg == "-m" || arg == "--detect-only") { opts.detectOnly = true; continue; }
    if (arg == "-l" || arg == "--level") { if (i + 1 < argc) opts.modelLevel = std::atoi(argv[++i]); continue; }
    if (arg == "-t" || arg == "--type") { if (i + 1 < argc) opts.modelType = std::atoi(argv[++i]); continue; }
    if (arg == "-g" || arg == "--gpu") { opts.useGPU = 1; continue; }
    if (arg == "-v" || arg == "--verbose") { opts.verbose = true; continue; }
    if (arg == "-c" || arg == "--clean") { opts.cleanMode = true; continue; }
    if (arg == "-y" || arg == "--yes") { opts.cleanYes = true; continue; }
    std::cerr << "未知参数: " << arg << std::endl;
    printHelp(argv[0]);
    return false;
  }
  return true;
}

// 生成默认输出路径
std::string generateOutputPath(const std::string& inputPath) {
  fs::path p(inputPath);
  fs::path stem = p.stem();
  fs::path ext = p.extension();
  std::string outputName = stem.string() + "_inpainted" + ext.string();
  p.replace_filename(outputName);
  return p.string();
}

// 创建并初始化 IWatermarkRemoval
std::unique_ptr<IWatermarkRemoval> createRemover(const Options& opts) {
  std::unique_ptr<IWatermarkRemoval> remover(AvoxManager::Get().watermarkRemovalHub.create("inpaint"));
  if (!remover) {
    std::cerr << "创建 WatermarkRemoval 失败 (avox_cv 插件未加载?)\n";
    return nullptr;
  }
  // 配置
  ModelLevel level = ModelLevel::base;
  if (opts.modelLevel == 0) level = ModelLevel::mini;
  else if (opts.modelLevel == 2) level = ModelLevel::high;
  InpaintMode type = InpaintMode::lama;
  if (opts.modelType == 1) type = InpaintMode::aotgan;
  remover->setModelLevel(level);
  remover->setInpaintMode(type);
  remover->setUseGPU(opts.useGPU != 0);
  remover->setDetectThreshold(0.15f);
  remover->setMaskDilate(20);
  // 加载模型
  if (!remover->open()) {
    std::cerr << "加载 inpaint 模型失败\n";
    return nullptr;
  }
  return remover;
}

// 打印检测结果
void printDetectionResult(IWatermarkRemoval* remover) {
  int count = remover->getWatermarkCount();
  std::cout << "  检测到 " << count << " 个水印\n";
  for (int i = 0; i < count; i++) {
    float x, y, w, h;
    if (remover->getWatermarkBBox(i, &x, &y, &w, &h)) {
      std::cout << "    [" << i << "] x=" << x << " y=" << y
                << " w=" << w << " h=" << h << "\n";
    }
  }
}

// 打印性能数据
void printTiming(IWatermarkRemoval* remover) {
  std::cout << "  性能: detect=" << remover->getDetectTimeMs()
            << "ms, inpaint=" << remover->getInpaintTimeMs() << "ms\n";
}

// 解析手动区域字符串 -> 生成 mask IImageBuffer
std::unique_ptr<IImageBuffer> createMaskFromRegions(const char* regions, int width, int height) {
  if (!regions || strlen(regions) == 0) return nullptr;
  // 解析区域
  std::vector<std::array<int, 4>> bboxes;
  std::string str(regions);
  std::stringstream ss(str);
  std::string segment;
  while (std::getline(ss, segment, ';')) {
    if (segment.empty()) continue;
    int x = 0, y = 0, w = 0, h = 0;
    if (sscanf(segment.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0) {
      bboxes.push_back(std::array<int, 4>{x, y, w, h});
    }
  }
  if (bboxes.empty()) return nullptr;
  // 生成 mask 数据
  auto maskBuf = std::unique_ptr<IImageBuffer>(createImageBuffer());
  ImageFormat maskFormat;
  maskFormat.width = width;
  maskFormat.height = height;
  maskFormat.imageType = ImageType::r8;
  maskFormat.rowPitch = width;
  maskBuf->setImageFormat(maskFormat);
  // 填充 mask
  uint8_t* maskData = maskBuf->getPointer();
  if (maskData) {
    memset(maskData, 0, width * height);
    int dilate = 2;
    for (const auto& bbox : bboxes) {
      int x1 = std::max(0, bbox[0] - dilate);
      int y1 = std::max(0, bbox[1] - dilate);
      int x2 = std::min(width, bbox[0] + bbox[2] + dilate);
      int y2 = std::min(height, bbox[1] + bbox[3] + dilate);
      for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
          maskData[y * width + x] = 255;
        }
      }
    }
  }
  return maskBuf;
}

// 处理单张图片
bool processImage(IWatermarkRemoval* remover, const std::string& inputPath,
                  const std::string& outputPath, const std::string& regions,
                  const std::string& maskPath, const Options& opts) {
  std::cout << "\n========================================\n";
  std::cout << "处理图片: " << inputPath << std::endl;

  // 创建 IImageBuffer
  std::unique_ptr<IImageBuffer> inputBuf(createImageBuffer());
  std::unique_ptr<IImageBuffer> outputBuf(createImageBuffer());
  if (!inputBuf || !outputBuf) {
    std::cerr << "创建 ImageBuffer 失败\n";
    return false;
  }
  // 加载图片
  if (!loadImagePath(inputPath.c_str(), inputBuf.get())) {
    std::cerr << "加载图片失败: " << inputPath << std::endl;
    return false;
  }
  auto format = inputBuf->getImageFormat();
  std::cout << "图片: " << format.width << "x" << format.height
            << ", type=" << getImageTypeStr(format.imageType) << std::endl;
  outputBuf->setImageFormat(format);

  bool success = false;

  // 使用外部 mask 进行修复
  if (!maskPath.empty()) {
    std::unique_ptr<IImageBuffer> maskBuf(createImageBuffer());
    if (!loadImagePath(maskPath.c_str(), maskBuf.get())) {
      std::cerr << "加载 mask 失败: " << maskPath << std::endl;
      return false;
    }
    success = remover->inpaint(inputBuf.get(), maskBuf.get(), outputBuf.get());
  }
  // 只检测模式
  else if (opts.detectOnly) {
    std::unique_ptr<IImageBuffer> maskBuf(createImageBuffer());
    maskBuf->setImageFormat(format);
    success = remover->detect(inputBuf.get(), maskBuf.get());
    if (success && maskBuf->getPointer()) {
      // 保存 mask
      std::string maskOut = inputPath;
      size_t dotPos = maskOut.rfind('.');
      if (dotPos != std::string::npos) {
        maskOut = maskOut.substr(0, dotPos) + "_mask" + maskOut.substr(dotPos);
      }
      saveImagePath(maskOut.c_str(), maskBuf.get());
      std::cout << "保存 mask: " << maskOut << std::endl;
    }
  }
  // 手动指定区域
  else if (!regions.empty()) {
    auto maskBuf = createMaskFromRegions(regions.c_str(), format.width, format.height);
    if (!maskBuf) {
      std::cerr << "解析区域失败\n";
      return false;
    }
    success = remover->inpaint(inputBuf.get(), maskBuf.get(), outputBuf.get());
  }
  // 自动检测+修复
  else {
    success = remover->process(inputBuf.get(), outputBuf.get());
  }

  if (!success) {
    std::cerr << "处理图片失败\n";
    return false;
  }
  // 保存结果
  std::string outPath = outputPath.empty() ? generateOutputPath(inputPath) : outputPath;
  if (!saveImagePath(outPath.c_str(), outputBuf.get())) {
    std::cerr << "保存图片失败: " << outPath << std::endl;
    return false;
  }
  std::cout << "保存图片到: " << outPath << std::endl;
  if (opts.verbose) {
    printDetectionResult(remover);
    printTiming(remover);
  }
  return true;
}

// 批量处理目录中的图片
bool processDirectory(IWatermarkRemoval* remover, const std::string& dirPath, const Options& opts) {
  if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
    std::cerr << "目录不存在: " << dirPath << std::endl;
    return false;
  }
  std::cout << "\n========================================\n";
  std::cout << "批量处理目录: " << dirPath << std::endl;
  std::vector<std::string> imageFiles;
  for (const auto& entry : fs::directory_iterator(dirPath)) {
    if (entry.is_regular_file()) {
      std::string ext = entry.path().extension().string();
      if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" ||
          ext == ".JPG" || ext == ".JPEG" || ext == ".PNG" || ext == ".BMP") {
        imageFiles.push_back(entry.path().string());
      }
    }
  }
  if (imageFiles.empty()) {
    std::cout << "目录中没有找到图片文件\n";
    return true;
  }
  std::cout << "找到 " << imageFiles.size() << " 个图片文件\n";
  int successCount = 0;
  for (const auto& imgPath : imageFiles) {
    if (processImage(remover, imgPath, generateOutputPath(imgPath), opts.regions, opts.maskPath, opts)) {
      successCount++;
    }
  }
  std::cout << "\n批量处理完成: " << successCount << "/" << imageFiles.size() << " 成功\n";
  return successCount == static_cast<int>(imageFiles.size());
}

// 查找默认测试图片
std::string findDefaultImage() {
  std::vector<std::string> searchPaths;
#ifdef _WIN32
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
    fs::path exeDir = fs::path(exePath).parent_path();
    searchPaths.push_back(exeDir.string());
    searchPaths.push_back(exeDir.parent_path().string());
  }
  searchPaths.push_back("D:/Work/github/avox");
#else
  char exePath[1024] = {0};
  ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
  if (len > 0) {
    exePath[len] = '\0';
    fs::path exeDir = fs::path(exePath).parent_path();
    searchPaths.push_back(exeDir.string());
    searchPaths.push_back(exeDir.parent_path().string());
  }
#endif
  searchPaths.push_back(".");
  searchPaths.push_back(fs::current_path().string());
  std::vector<std::string> testImages = {
      "data/images/realistic/logo/logo_001.png",
      "data/images/realistic/text/text_001.png",
  };
  for (const auto& basePath : searchPaths) {
    if (basePath.empty() || !fs::exists(basePath)) continue;
    for (const auto& img : testImages) {
      fs::path fullPath = fs::path(basePath) / img;
      if (fs::exists(fullPath)) return fullPath.string();
    }
  }
  return "";
}

// 查找默认数据目录
std::string findDefaultDataDir() {
  std::vector<std::string> searchPaths;
#ifdef _WIN32
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
    fs::path exeDir = fs::path(exePath).parent_path();
    searchPaths.push_back(exeDir.string());
    searchPaths.push_back(exeDir.parent_path().string());
  }
#else
  char exePath[1024] = {0};
  ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
  if (len > 0) {
    exePath[len] = '\0';
    fs::path exeDir = fs::path(exePath).parent_path();
    searchPaths.push_back(exeDir.string());
    searchPaths.push_back(exeDir.parent_path().string());
  }
#endif
  searchPaths.push_back(".");
  searchPaths.push_back(fs::current_path().string());
  for (const auto& basePath : searchPaths) {
    if (basePath.empty() || !fs::exists(basePath)) continue;
    fs::path fullPath = fs::path(basePath) / "data" / "images";
    if (fs::exists(fullPath) && fs::is_directory(fullPath)) return fullPath.string();
  }
  return "";
}

// 清理生成的文件
bool cleanGeneratedFiles(const std::string& dirPath, bool autoConfirm) {
  if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
    std::cerr << "目录不存在: " << dirPath << std::endl;
    return false;
  }
  const std::vector<std::string> patterns = {"_inpainted", "_clean", "_mask"};
  std::vector<std::string> filesToDelete;
  for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
    if (entry.is_regular_file()) {
      std::string stem = entry.path().stem().string();
      for (const auto& pattern : patterns) {
        if (stem.find(pattern) != std::string::npos) {
          filesToDelete.push_back(entry.path().string());
          break;
        }
      }
    }
  }
  if (filesToDelete.empty()) {
    std::cout << "没有找到需要清理的文件\n";
    return true;
  }
  std::cout << "找到 " << filesToDelete.size() << " 个需要清理的文件\n";
  if (!autoConfirm) {
    std::cout << "确认删除? (y/n): ";
    char confirm;
    std::cin >> confirm;
    if (confirm != 'y' && confirm != 'Y') return false;
  }
  int deletedCount = 0;
  for (const auto& f : filesToDelete) {
    if (fs::remove(f)) deletedCount++;
  }
  std::cout << "删除 " << deletedCount << "/" << filesToDelete.size() << " 个文件\n";
  return true;
}

int main(int argc, char* argv[]) {
  std::cout << "=== 水印去除测试程序 (IWatermarkRemoval) ===" << std::endl;
  Options opts;
  if (!parseArgs(argc, argv, opts)) return 0;

  // 清理模式
  if (opts.cleanMode) {
    std::string targetDir = opts.directory.empty() ? findDefaultDataDir() : opts.directory;
    if (targetDir.empty()) { std::cerr << "未找到数据目录\n"; return 1; }
    return cleanGeneratedFiles(targetDir, opts.cleanYes) ? 0 : 1;
  }

  // 初始化
  std::cout << "初始化: level=" << opts.modelLevel
            << " type=" << (opts.modelType == 0 ? "lama" : "aotgan")
            << " gpu=" << opts.useGPU << std::endl;
  auto remover = createRemover(opts);
  if (!remover) {
    std::cerr << "初始化失败\n";
    return 1;
  }

  // 处理
  bool success = false;
  if (!opts.directory.empty()) {
    success = processDirectory(remover.get(), opts.directory, opts);
  } else if (!opts.inputPath.empty()) {
    success = processImage(remover.get(), opts.inputPath, opts.outputPath,
                           opts.regions, opts.maskPath, opts);
  } else {
    std::string defaultImage = findDefaultImage();
    if (defaultImage.empty()) {
      std::cerr << "未找到默认测试图片，请使用 -i 参数指定\n";
      return 1;
    }
    std::cout << "使用默认测试图片: " << defaultImage << std::endl;
    success = processImage(remover.get(), defaultImage, opts.outputPath,
                           opts.regions, opts.maskPath, opts);
  }

  if (opts.verbose) {
    printDetectionResult(remover.get());
    printTiming(remover.get());
  }

  // remover unique_ptr 自动释放
  std::cout << "\n=== 测试完成 ===" << std::endl;
  return success ? 0 : 1;
}
