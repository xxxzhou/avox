/**
 * @file CmdVision.cpp
 * @brief vision 子命令 - 静态图像识别可视化
 *
 * 针对静态场景图做视觉识别, 两种识别器按参数自动选择:
 *   - OCR 文字识别 (ITextRecognizer, PP-OCRv6): 无 -t 时默认
 *   - 模板/图标匹配 (ITemplateMatcher, cv::matchTemplate): 提供 -t 时
 * --mode ocr|template 可显式覆盖。两种结果经 collectBox 归一为框+标签,
 * drawBoxes/drawLabels 与具体识别器解耦。
 *
 * IImageRender 开 Vulkan 窗口显示:
 *   IGeometryLayer 画框 (OCR=检测框 / 模板=命中框),
 *   IFontLayer 画标签 (OCR=识别文字 / 模板=score)。
 * 键控实时改色 (借鉴 cmdPlay 颜色循环):
 *   C=标签颜色  I=框颜色  R=重新识别(截图)  O=显隐  Q=quit
 * 框 与 标签 共用同一颜色表, 但各持独立索引, 默认取不同颜色 (框=红, 标签=绿, 与 cmdPlay 一致)。
 * 注: IImageRender 为静态图渲染, 改色/重画后需再 render() 才上屏。
 */

#include "CmdVision.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "avox_cmd/CmdHelper.hpp"
#include "avox/AvoxVision.h"
#include "avox/module/AvoxManager.hpp"
#include "avox/AvoxLayer.h"
#include "avox_vulkan/VkExport.h"
#include "avox_freetype/FreetypeExport.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace avox {

Command cmdVision() {
  Command cmd;
  cmd.name = "vision";
  cmd.desc = "静态图像识别可视化: OCR/模板匹配, 框+标签调参 (找字+点击用 ops -t)";
  // 场景图 (必需)
  cmd.parser.addArg(
      {"-s", "--scene", ArgType::String, true, "场景图路径", ""});
  // 模板图 (可选, 可多次 -t a -t b; 提供则走模板匹配)
  cmd.parser.addArg(
      {"-t", "--template", ArgType::String, false,
       "模板图路径 (可多次); 提供则走模板匹配, 否则 OCR", ""});
  // 模式显式覆盖
  cmd.parser.addArg({"", "--mode", ArgType::String, false,
                     "ocr|template (默认: 有 -t 则 template 否则 ocr)", ""});
  // 阈值 (OCR 默认 0.3 / 模板 默认 0.7)
  cmd.parser.addArg({"-e", "--threshold", ArgType::Number, false,
                     "阈值 [0,1] (OCR 默认 0.3 / 模板 默认 0.7)", ""});
  // GPU 推理 (仅 OCR)
  cmd.parser.addArg({"-g", "--gpu", ArgType::Boolean, false, "OCR GPU 推理", ""});
  // 模板方法 (仅模板)
  cmd.parser.addArg({"-m", "--method", ArgType::Int, false,
                     "模板方法: 0=默认 1=sqdiffNormed 2=ccorrNormed 3=ccoeffNormed",
                     "0"});
  // green mask (仅模板)
  cmd.parser.addArg({"", "--green", ArgType::Boolean, false,
                     "模板 green_mask (模板纯绿区域当透明)", ""});
  // 感兴趣区域 (两种模式通用)
  cmd.parser.addArg(
      {"-r", "--roi", ArgType::String, false, "感兴趣区域 x,y,w,h", ""});
  // NMS IoU (仅模板)
  cmd.parser.addArg({"", "--nms", ArgType::Number, false,
                     "模板 NMS 去重 IoU 阈值 (默认 0.2, 0=关闭)", "0.2"});

  cmd.run = [](const ParsedArgs& args) -> int {
    std::string scenePath = args.getString("scene");
    // 模板路径列表 (getStringList 收集多次 -t)
    const std::vector<std::string>& tmplPaths = args.getStringList("template");
    // 模式: 显式 --mode > 自动 (有模板则 template 否则 ocr)
    std::string mode = args.getString("mode");
    if (mode.empty()) mode = tmplPaths.empty() ? "ocr" : "template";
    if (mode != "ocr" && mode != "template") {
      fprintf(stderr, "--mode 取值仅支持 ocr|template, 实际: %s\n", mode.c_str());
      return 1;
    }
    if (mode == "template" && tmplPaths.empty()) {
      fprintf(stderr, "模板匹配模式需要至少一个 -t/--template\n");
      return 1;
    }
    if (mode == "ocr" && !tmplPaths.empty()) {
      fprintf(stderr,
              "提示: OCR 模式忽略 %zu 个 --template (用 --mode template 切换)\n",
              tmplPaths.size());
    }
    bool isTemplate = (mode == "template");
    // 阈值 (按 mode 取默认)
    double threshold;
    if (args.has("threshold")) {
      threshold = (double)args.getFloat("threshold", 0.0f);
    } else {
      threshold = isTemplate ? 0.7 : 0.3;
    }
    bool gpu = args.getBool("gpu");
    // 场景图
    std::unique_ptr<IImageBuffer> scene(createImageBuffer());
    if (!loadImagePath(scenePath.c_str(), scene.get())) {
      fprintf(stderr, "场景图加载失败: %s\n", scenePath.c_str());
      return 1;
    }
    // 识别器: OCR 用 createTextRecognizer(), 模板用 templateMatcherHub.create("opencv")
    std::unique_ptr<ITextRecognizer> ocr;
    std::unique_ptr<ITemplateMatcher> matcher;
    if (isTemplate) {
      matcher.reset(AvoxManager::Get().templateMatcherHub.create("opencv"));
      if (!matcher) {
        fprintf(stderr, "创建 TemplateMatcher 失败 (avox_opencv 插件未加载?)\n");
        return 1;
      }
      int method = args.getInt("method", 0);
      if (method >= 1 && method <= 3)
        matcher->setMethod(static_cast<TemplateMatchMethod>(method));
      matcher->setGreenMask(args.getBool("green"));
      matcher->setNmsIoU(args.getFloat("nms", 0.2f));
      std::string roi = args.getString("roi");
      if (!roi.empty()) {
        int rx, ry, rw, rh;
        if (sscanf(roi.c_str(), "%d,%d,%d,%d", &rx, &ry, &rw, &rh) == 4)
          matcher->setRoi(rx, ry, rw, rh);
      }
      // 加模板 (addTemplatePath 内部 createImageBuffer+loadImagePath+深拷贝, 无需外部保活)
      for (const auto& p : tmplPaths) {
        if (matcher->addTemplatePath(p.c_str(), threshold) < 0) {
          fprintf(stderr, "模板图加载失败: %s (%s)\n", p.c_str(), matcher->getLastError());
          return 1;
        }
      }
    } else {
      ocr.reset(createTextRecognizer());
      if (!ocr) {
        fprintf(stderr, "创建 TextRecognizer 失败 (avox_ocr 插件未加载?)\n");
        return 1;
      }
      ocr->setThreshold(threshold);
      ocr->setUseGpu(gpu);
      std::string roi = args.getString("roi");
      if (!roi.empty()) {
        int rx, ry, rw, rh;
        if (sscanf(roi.c_str(), "%d,%d,%d,%d", &rx, &ry, &rw, &rh) == 4)
          ocr->setRoi(rx, ry, rw, rh);
      }

    }
#ifndef AVOX_ENABLE_VULKAN
    fprintf(stderr, "vision 可视化需要 AVOX_ENABLE_VULKAN\n");
    return 1;
#else
    // 统一框+标签 (两种识别器结果归一为 VisionBox, 让绘制与 mode 解耦)
    struct VisionBox {
      int x, y, w, h;
      char label[256];
    };
    auto collectBox = [&](int i, VisionBox& out) -> bool {
      if (isTemplate) {
        MatchResult r;
        if (!matcher->getMatch(i, &r)) return false;
        out.x = r.x;
        out.y = r.y;
        out.w = r.w;
        out.h = r.h;
        snprintf(out.label, sizeof(out.label), "%.2f", r.score);
        return true;
      }
      OcrResult r;
      const char* text = ocr->getMatch(i, &r);
      if (!text) return false;
      out.x = r.x;
      out.y = r.y;
      out.w = r.w;
      out.h = r.h;
      snprintf(out.label, sizeof(out.label), "%s", text);
      return true;
    };
    auto getMatchCount = [&]() -> int {
      return isTemplate ? matcher->getMatchCount() : ocr->getMatchCount();
    };
    auto getMatchTimeMs = [&]() -> float {
      return isTemplate ? matcher->getMatchTimeMs() : ocr->getMatchTimeMs();
    };
    auto reco = [&](IImageBuffer* img) -> int {
      return isTemplate ? matcher->match(img) : ocr->recognize(img);
    };
    // 静态图渲染器 + Vulkan 窗口 (setSurface(nullptr) 自动创建窗口)
    IImageRender* ir = createImageRender();
    ISurfaceRender* wr = ir->getSurfaceRender();
    wr->setSurface(nullptr);
    // 颜色表: 框 与 标签 共用, 各持独立索引, 默认不同
    struct OsdColor {
      float r, g, b;
      const char* name;
    };
    static const OsdColor kColors[] = {
        {0.2f, 0.6f, 1.0f, "亮蓝"}, {0.0f, 1.0f, 0.4f, "绿"},
        {1.0f, 0.8f, 0.0f, "橙"},   {1.0f, 0.3f, 0.3f, "红"},
        {0.8f, 0.4f, 1.0f, "紫"},   {1.0f, 1.0f, 1.0f, "白"},
    };
    constexpr int kColorCount = sizeof(kColors) / sizeof(kColors[0]);
    int geoColorIdx = 3;   // 框 默认红 (与 cmdPlay 几何装饰配色一致)
    int textColorIdx = 1;  // 标签 默认绿 — 与框默认不同 (与 cmdPlay OSD 文本一致)
    // 叠加层 (随 render 持有; enable/disable 仅控制进/出执行链, 不释放对象)
    IGeometryLayer* geoLayer = enableRenderGeometry(wr);
    // 框线粗细主控: setScale 决定 canvas 分辨率(=帧/(scale*dpiScale))。
    // rasterLine core 固定 2px(canvas), 经 tscale 放大后 = 2*tscale 帧px;
    // 默认 scale=4 → 框线≈8px(粗, 遮盖文字); setThreshold 只能切 AA 渐变, 切不掉 core。
    // scale=1 → canvas≈帧分辨率, tscale≈1, 框线≈2px(细)。
    if (geoLayer) geoLayer->setScale(1.0f);
    IFontLayer* fontLayer = nullptr;
#ifdef AVOX_ENABLE_FREETYPE
    fontLayer = enableRenderFont(wr);
#endif
    // 最近一次识别图像尺寸 (框/标签 归一化基准); lastShot 非 null 则渲染截图
    float curW = 0.0f;
    float curH = 0.0f;
    std::unique_ptr<IImageBuffer> lastShot;
    // 画框 (几何层, 归一化坐标)
    auto drawBoxes = [&]() {
      if (!geoLayer || curW <= 0 || curH <= 0) return;
      const auto& c = kColors[geoColorIdx];
      geoLayer->setColor(c.r, c.g, c.b);
      geoLayer->setThreshold(0.9f);  // 仅切 AA 渐变(线宽主控是 setScale, 见初始化)
      geoLayer->clear();
      int n = getMatchCount();
      for (int i = 0; i < n; ++i) {
        VisionBox b;
        if (!collectBox(i, b)) continue;
        geoLayer->drawRect(b.x / curW, b.y / curH, (b.x + b.w) / curW,
                           (b.y + b.h) / curH);
      }
    };
    // 画标签 (字体层, 每条独立 layout, 定位在框上方)
    auto drawLabels = [&]() {
      if (!fontLayer || curW <= 0 || curH <= 0) return;
      fontLayer->setFont("simhei.ttf", 24);
      const auto& c = kColors[textColorIdx];
      fontLayer->setColor(c.r, c.g, c.b, 0.0f);
      int n = getMatchCount();
      for (int i = 0; i < n; ++i) {
        VisionBox b;
        if (!collectBox(i, b)) continue;
        float tx = b.x / curW;
        float ty = b.y / curH - 0.02f;
        if (ty < 0) ty = 0;
        FontLayout l = {};
        l.alignment.horizontal = HAlignType::left;
        l.alignment.vertical = VAlignType::bottom;
        l.x = tx;
        l.y = ty;
        l.width = 0.8f;
        l.height = 0.05f;
        fontLayer->updateLayout(i, l);
        fontLayer->setTextLayout(i);
        fontLayer->drawText(b.label);
      }
    };
    // 重画+重渲当前图 (lastShot 优先, 否则原图); 静态图改色/重画后必须再 render 才上屏
    auto present = [&]() {
      IImageBuffer* img = lastShot ? lastShot.get() : scene.get();
      ImageFormat f = img->getImageFormat();
      curW = (float)f.width;
      curH = (float)f.height;
      drawBoxes();
      drawLabels();
      ir->render(img);
    };
    // 初始识别 + 首帧
    int n = reco(scene.get());
    printf("avox_cli vision (%s)\n", isTemplate ? "template" : "ocr");
    printf("  scene:     %s\n", scenePath.c_str());
    if (isTemplate) {
      printf("  模板: %zu 个, 阈值 %.2f\n", tmplPaths.size(), threshold);
      printf("  method: %d  green: %s  nms: %.2f\n", args.getInt("method", 0),
             args.getBool("green") ? "on" : "off", args.getFloat("nms", 0.2f));
    } else {
      printf("  threshold: %.2f  gpu: %s\n", threshold, gpu ? "on" : "off");
    }
    printf("  识别: %d 条, 耗时 %.0f ms\n", n, getMatchTimeMs());
    present();
    printf("  标签颜色: %s (C 切换)   框颜色: %s (I 切换)\n",
           kColors[textColorIdx].name, kColors[geoColorIdx].name);
    printf("  keys: C=标签颜色 I=框颜色 R=重新识别 O=显隐 Q=quit\n");
    if (!fontLayer) printf("  (IFontLayer 不可用: AVOX_ENABLE_FREETYPE 未编译)\n");
    // 运行循环
    gCmdRunning = true;
    std::signal(SIGINT, cmdSignalHandler);
    std::signal(SIGTERM, cmdSignalHandler);
    bool layersVisible = true;
#ifdef _WIN32
    while (gCmdRunning) {
      // 窗口被关 (点X/Alt+F4) -> 退出
      if (wr->getSurface() && !IsWindow((HWND)wr->getSurface())) {
        printf("Window closed\n");
        break;
      }
      MSG msg;
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT || msg.message == WM_CLOSE) {
          gCmdRunning = false;
          break;
        }
        if (msg.message == WM_KEYDOWN) {
          int vk = (int)msg.wParam;
          switch (vk) {
            case 'Q':
            case VK_ESCAPE:
              gCmdRunning = false;
              break;
            case 'C':  // 标签颜色循环 (独立于框)
              textColorIdx = (textColorIdx + 1) % kColorCount;
              present();
              printf("  标签颜色: %s\n", kColors[textColorIdx].name);
              break;
            case 'I':  // 框颜色循环 (独立于标签)
              geoColorIdx = (geoColorIdx + 1) % kColorCount;
              present();
              printf("  框颜色: %s\n", kColors[geoColorIdx].name);
              break;
            case 'R': {  // 重新识别: 截图 -> 识别 -> 重渲 (验证 screenShot 全链路)
              lastShot.reset(createImageBuffer());
              if (wr->screenShot(lastShot.get())) {
                int nn = reco(lastShot.get());
                printf("  重新识别: %d 条, 耗时 %.0f ms\n", nn, getMatchTimeMs());
                present();
              } else {
                lastShot.reset();
                printf("  screenshot 失败\n");
              }
              break;
            }
            case 'O': {  // 显隐 框+标签
              layersVisible = !layersVisible;
              if (layersVisible) {
                geoLayer = enableRenderGeometry(wr);
#ifdef AVOX_ENABLE_FREETYPE
                fontLayer = enableRenderFont(wr);
#endif
              } else {
                if (geoLayer) {
                  disableRenderGeometry(wr);
                  geoLayer = nullptr;
                }
#ifdef AVOX_ENABLE_FREETYPE
                if (fontLayer) {
                  disableRenderFont(wr);
                  fontLayer = nullptr;
                }
#endif
              }
              present();
              printf("  layers: %s\n", layersVisible ? "on" : "off");
              break;
            }
            default:
              break;
          }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
#else
    (void)layersVisible;
    printf("  (non-Windows: 已渲染一帧, 无交互循环)\n");
#endif
    delete ir;
    return 0;
#endif  // AVOX_ENABLE_VULKAN
  };
  return cmd;
}

}
