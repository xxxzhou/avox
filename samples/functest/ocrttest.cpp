/**
 * @file ocrttest.cpp
 * @brief OCR 文字识别测试 (ITextRecognizer / PP-OCRv6)
 *
 * 在场景图中识别文字及其位置 (供 Agent 按文字点击)。
 * 模型从 assets/models/ocr/ 自动加载。
 *
 * 模式:
 *   默认       识别并打印结果到控制台
 *   -o <path>  OpenCV 可视化: 在场景图上画框+文字保存结果图
 *   -v         Vulkan 可视化: 打开窗口, 用 IFontLayer + IGeometryLayer 实时叠加
 *
 * 用法:
 *   ocrttest -s scene.png
 *   ocrttest -s scene.png -t 0.3       # 检测阈值
 *   ocrttest -s scene.png -g           # GPU 推理
 *   ocrttest -s scene.png -o out.png   # OpenCV 可视化输出
 *   ocrttest -s scene.png -v           # Vulkan 窗口实时叠加
 */

#include <iostream>
#include <string>
#include <memory>

#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/AvoxVideo.h"
#include "avox/AvoxPlayer.h"

#ifdef AVOX_ENABLE_VULKAN
#include "avox_vulkan/VkExport.h"
#endif

#ifdef AVOX_ENABLE_FREETYPE
#include "avox_freetype/FreetypeExport.h"
#endif

#include <opencv2/opencv.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace avox;

struct Opts {
  std::string scene;
  std::string output;
  double threshold = 0.3;
  bool gpu = false;
  bool visual = false;  // -v: Vulkan 窗口可视化
};

static void printHelp(const char* prog) {
  std::cout << "用法: " << prog << " -s <scene.png> [-t <thresh>] [-g] [-o <out.png>] [-v]\n\n"
            << "选项:\n"
            << "  -s <path>   场景图\n"
            << "  -t <double> 检测阈值 [0,1] (默认 0.3)\n"
            << "  -g          GPU 推理\n"
            << "  -o <path>   OpenCV 可视化输出 (画框+文字, 保存结果图)\n"
            << "  -v          Vulkan 窗口可视化 (IFontLayer + IGeometryLayer 实时叠加)\n"
            << std::endl;
}

static bool parseArgs(int argc, char* argv[], Opts& o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h") { printHelp(argv[0]); return false; }
    if (a == "-s" && i + 1 < argc) { o.scene = argv[++i]; continue; }
    if (a == "-t" && i + 1 < argc) { o.threshold = std::stod(argv[++i]); continue; }
    if (a == "-g") { o.gpu = true; continue; }
    if (a == "-o" && i + 1 < argc) { o.output = argv[++i]; continue; }
    if (a == "-v") { o.visual = true; continue; }
    std::cerr << "未知参数: " << a << "\n";
    printHelp(argv[0]);
    return false;
  }
  if (o.scene.empty()) {
    std::cerr << "缺少必需参数 (-s)\n";
    printHelp(argv[0]);
    return false;
  }
  return true;
}

// OCR 识别 + 打印结果
static int runOcr(ITextRecognizer* ocr, IImageBuffer* scene, const Opts& opts) {
  int n = ocr->recognize(scene);
  std::cout << "识别条数: " << n << "\n";
  std::cout << "耗时: " << ocr->getMatchTimeMs() << " ms\n";
  for (int i = 0; i < n; ++i) {
    OcrResult r;
    const char* text = ocr->getMatch(i, &r);
    vec2i center = getOcrCenter(r);
    std::cout << "  [" << i << "] \"" << (text ? text : "") << "\" @ (" << center.x << "," << center.y << ") "
              << "框(" << r.x << "," << r.y << "," << r.w << "," << r.h << ") "
              << "score=" << r.score << "\n";
  }
  const char* err = ocr->getLastError();
  if (err && err[0]) std::cout << "提示: " << err << "\n";
  return n;
}

// OpenCV 可视化: 在场景图上画框 + 文字, 保存结果图
static void saveVisualization(ITextRecognizer* ocr, const std::string& scenePath, int n) {
  if (n <= 0) return;
  cv::Mat img = cv::imread(scenePath);
  if (img.empty()) {
    std::cerr << "可视化: imread 失败\n";
    return;
  }
  for (int i = 0; i < n; ++i) {
    OcrResult r;
    const char* text = ocr->getMatch(i, &r);
    // 绿色框
    cv::rectangle(img, cv::Point(r.x, r.y), cv::Point(r.x + r.w, r.y + r.h),
                  cv::Scalar(0, 255, 0), 2);
    // 框上方写文字 (红底白字)
    int baseline = 0;
    double fontScale = 0.5;
    int thickness = 1;
    const char* textBuf = text ? text : "";
    cv::Size tsz = cv::getTextSize(textBuf, cv::FONT_HERSHEY_SIMPLEX, fontScale, thickness, &baseline);
    int textY = std::max(r.y - 4, tsz.height + 2);
    cv::rectangle(img, cv::Point(r.x, textY - tsz.height - 2),
                  cv::Point(r.x + tsz.width, textY + 2), cv::Scalar(0, 0, 200), -1);
    cv::putText(img, textBuf, cv::Point(r.x, textY), cv::FONT_HERSHEY_SIMPLEX,
                fontScale, cv::Scalar(255, 255, 255), thickness);
  }
  std::string outPath = scenePath + ".ocr.png";
  if (cv::imwrite(outPath, img)) {
    std::cout << "可视化已保存: " << outPath << "\n";
  } else {
    std::cerr << "可视化保存失败: " << outPath << "\n";
  }
}

// Vulkan 窗口可视化: 用 IImageRender 显示图片, IFontLayer + IGeometryLayer 叠加 OCR 结果
static void runVisual(ITextRecognizer* ocr, IImageBuffer* scene, const Opts& opts) {
#ifdef AVOX_ENABLE_VULKAN
  // 先做 OCR 识别
  int n = ocr->recognize(scene);
  std::cout << "识别条数: " << n << "  耗时: " << ocr->getMatchTimeMs() << " ms\n";
  if (n <= 0) {
    std::cout << "无识别结果, 窗口仅显示原图\n";
  }
  // 获取图像尺寸 (用于坐标归一化)
  ImageFormat fmt = scene->getImageFormat();
  float imgW = static_cast<float>(fmt.width);
  float imgH = static_cast<float>(fmt.height);
  if (imgW <= 0 || imgH <= 0) {
    std::cerr << "无法获取图像尺寸\n";
    return;
  }
  // 用 IImageRender 显示静态图片
  IImageRender* ir = createImageRender();
  ISurfaceRender* wr = ir->getSurfaceRender();
  wr->setSurface(nullptr);  // nullptr = 自动创建 Vulkan 窗口
  // 启用 IGeometryLayer: 画检测框 (绿色)
  IGeometryLayer* geoLayer = enableRenderGeometry(wr);
  if (geoLayer) {
    geoLayer->setColor(0.0f, 1.0f, 0.0f);  // 绿色
    geoLayer->setThreshold(0.5f);
    geoLayer->clear();
    for (int i = 0; i < n; ++i) {
      OcrResult r;
      ocr->getMatch(i, &r);
      // 像素坐标归一化到 [0,1]
      float x0 = r.x / imgW;
      float y0 = r.y / imgH;
      float x1 = (r.x + r.w) / imgW;
      float y1 = (r.y + r.h) / imgH;
      geoLayer->drawRect(x0, y0, x1, y1);
    }
    std::cout << "IGeometryLayer: 已画 " << n << " 个检测框\n";
  } else {
    std::cerr << "IGeometryLayer 不可用 (Vulkan 未启用?)\n";
  }
#ifdef AVOX_ENABLE_FREETYPE
  // 启用 IFontLayer: 在每个检测框上方画识别文字 (红色)
  IFontLayer* fontLayer = enableRenderFont(wr);
  if (fontLayer) {
    fontLayer->setFont("simhei.ttf", 14);
    fontLayer->setColor(1.0f, 0.0f, 0.0f, 0.0f);  // 红色, 不透明
    fontLayer->setScale(1.5f);
    for (int i = 0; i < n; ++i) {
      OcrResult r;
      const char* text = ocr->getMatch(i, &r);
      // 文字定位: 框上方, 归一化坐标
      float tx = r.x / imgW;
      float ty = r.y / imgH - 0.02f;  // 稍微上移
      if (ty < 0) ty = 0;
      FontLayout layout;
      layout.alignment.horizontal = HAlignType::left;
      layout.alignment.vertical = VAlignType::bottom;
      layout.x = tx;
      layout.y = ty;
      layout.width = 0.8f;
      layout.height = 0.05f;
      fontLayer->updateLayout(i, layout);
      fontLayer->setTextLayout(i);
      fontLayer->drawText(text ? text : "");
    }
    std::cout << "IFontLayer: 已画 " << n << " 个文字标签\n";
  } else {
    std::cerr << "IFontLayer 不可用 (Freetype 未启用?)\n";
  }
#else
  std::cerr << "IFontLayer 不可用 (AVOX_ENABLE_FREETYPE 未编译)\n";
#endif
  // 首次渲染: 将图片+叠加层显示到窗口
  ir->render(scene);
  // Win32 消息循环 (按 Q/ESC 退出)
  std::cout << "窗口已打开, 按 Q 或 ESC 退出\n";
  bool running = true;
  MSG msg;
  while (running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
      if (msg.message == WM_KEYDOWN) {
        int vk = (int)msg.wParam;
        if (vk == 'Q' || vk == VK_ESCAPE) {
          running = false;
          break;
        }
        // R: 重新识别 (截图 + OCR + 更新层)
        if (vk == 'R') {
          std::unique_ptr<IImageBuffer> shot(createImageBuffer());
          if (wr->screenShot(shot.get())) {
            int newN = ocr->recognize(shot.get());
            std::cout << "重新识别: " << newN << " 条, 耗时: " << ocr->getMatchTimeMs() << " ms\n";
            // 更新几何层
            if (geoLayer) {
              ImageFormat sf = shot->getImageFormat();
              float sw = static_cast<float>(sf.width);
              float sh = static_cast<float>(sf.height);
              if (sw > 0 && sh > 0) {
                geoLayer->clear();
                for (int i = 0; i < newN; ++i) {
                  OcrResult r;
                  ocr->getMatch(i, &r);
                  geoLayer->drawRect(r.x / sw, r.y / sh, (r.x + r.w) / sw, (r.y + r.h) / sh);
                }
              }
            }
#ifdef AVOX_ENABLE_FREETYPE
            // 更新文字层
            if (fontLayer) {
              ImageFormat sf = shot->getImageFormat();
              float sw = static_cast<float>(sf.width);
              float sh = static_cast<float>(sf.height);
              if (sw > 0 && sh > 0) {
                for (int i = 0; i < newN; ++i) {
                  OcrResult r;
                  const char* text = ocr->getMatch(i, &r);
                  float tx = r.x / sw;
                  float ty = r.y / sh - 0.02f;
                  if (ty < 0) ty = 0;
                  FontLayout layout;
                  layout.alignment.horizontal = HAlignType::left;
                  layout.alignment.vertical = VAlignType::bottom;
                  layout.x = tx;
                  layout.y = ty;
                  layout.width = 0.8f;
                  layout.height = 0.05f;
                  fontLayer->updateLayout(i, layout);
                  fontLayer->setTextLayout(i);
                  fontLayer->drawText(text ? text : "");
                }
              }
            }
#endif
            // 重新渲染
            ir->render(scene);
          }
        }
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  delete ir;
#else
  (void)ocr;
  (void)scene;
  std::cerr << "Vulkan 可视化需要 AVOX_ENABLE_VULKAN\n";
#endif
}

int main(int argc, char* argv[]) {
  std::cout << "=== OCR 文字识别测试 (ITextRecognizer / PP-OCRv6) ===\n";
  Opts opts;
  if (!parseArgs(argc, argv, opts)) return 0;

  std::unique_ptr<ITextRecognizer> ocr(createTextRecognizer());
  if (!ocr) {
    std::cerr << "创建 TextRecognizer 失败 (avox_ocr 插件未加载?)\n";
    return 1;
  }
  ocr->setThreshold(opts.threshold);
  ocr->setUseGpu(opts.gpu);

  std::unique_ptr<IImageBuffer> scene(createImageBuffer());
  if (!loadImagePath(opts.scene.c_str(), scene.get())) {
    std::cerr << "场景图加载失败: " << opts.scene << "\n";
    return 1;
  }

  if (opts.visual) {
    // Vulkan 窗口可视化模式
    runVisual(ocr.get(), scene.get(), opts);
  } else {
    // 默认: 识别 + 打印
    int n = runOcr(ocr.get(), scene.get(), opts);
    // OpenCV 可视化输出
    if (!opts.output.empty()) {
      saveVisualization(ocr.get(), opts.scene, n);
    }
  }
  return 0;
}
