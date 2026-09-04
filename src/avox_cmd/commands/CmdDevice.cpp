/**
 * @file CmdDevice.cpp
 * @brief device 子命令 - 设备枚举 + ISourcePlayer 采集预览/截图/录像
 *
 * 整合 sourceplaytest / sourcettstest 能力:
 *   1. 枚举视频/音频设备 (SDK 默认取平台值: Win=win_capture/wasapi,
 *      Android=and_ndkcamer2/android, iOS=ios_avf/ios; -vsdk 可选 win_mf 相机)
 *   2. 按索引或名称子串选择设备
 *   3. ISourcePlayer 开 Vulkan 窗口预览采集
 *   4. 键控: P=截图 R=录像(开/关) A=重开设备 Q=quit
 * 用法:
 *   avox_cli device -list                       # 仅列出设备后退出
 *   avox_cli device -list -vsdk win_mf          # 列出 MF 相机
 *   avox_cli device -vi 0 -ai 0                 # 选 0 号视频/音频设备预览
 *   avox_cli device -vname OBS                  # 按名称子串选视频设备
 *   avox_cli device -vi 1 -record D:/rec.mp4    # 预览并自动录像
 */

#include "CmdDevice.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "avox_cmd/CmdHelper.hpp"
#include "avox/Avox.hpp"
#include "avox/AvoxSource.h"
#include "avox/AvoxPlayer.h"
#include "avox/AvoxMuxer.h"
#include "avox/AvoxVideo.h"
#include "avox/module/LogHelper.hpp"
#include "avox/module/Time.hpp"
#include "avox_freetype/FreetypeExport.h"
#include "avox_vulkan/VkExport.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace avox {

namespace {
// 视频设备 SDK -> 字符串 (仅用于显示)
const char* vSdkStr(VDeviceSdk s) {
  switch (s) {
    case VDeviceSdk::win_capture: return "win_capture";
    case VDeviceSdk::win_mf: return "win_mf";
    case VDeviceSdk::and_ndkcamer2: return "and_ndkcamer2";
    case VDeviceSdk::ios_avf: return "ios_avf";
    default: return "none";
  }
}
// 音频设备 SDK -> 字符串 (仅用于显示)
const char* aSdkStr(ADeviceSdk s) {
  switch (s) {
    case ADeviceSdk::wasapi: return "wasapi";
    case ADeviceSdk::android: return "android";
    case ADeviceSdk::ios: return "ios";
    default: return "none";
  }
}
// 按名称子串在视频管理器里找设备, 找不到返回 -1
int findVideoByName(IVideoManager* mgr, const std::string& sub) {
  if (!mgr || sub.empty()) return -1;
  int n = mgr->getDeviceCount();
  for (int i = 0; i < n; ++i) {
    const char* name = mgr->getDevice(i)->getDeviceName();
    if (name && std::string(name).find(sub) != std::string::npos) return i;
  }
  return -1;
}
// 按名称子串在音频管理器里找设备, 找不到返回 -1
int findAudioByName(IAudioManager* mgr, const std::string& sub) {
  if (!mgr || sub.empty()) return -1;
  int n = mgr->getDeviceCount();
  for (int i = 0; i < n; ++i) {
    const char* name = mgr->getDevice(i)->getDeviceName();
    if (name && std::string(name).find(sub) != std::string::npos) return i;
  }
  return -1;
}
}  // namespace

Command cmdDevice() {
  Command cmd;
  cmd.name = "device";
  cmd.desc = "设备采集预览: 列设备(-list) / 摄像头·窗口·屏幕预览 / 录制(-record)";
  cmd.parser.addArg({"-list", "", ArgType::Boolean, false, "仅列出设备后退出", ""});
  cmd.parser.addArg({"-vsdk", "", ArgType::String, false,
                     "视频SDK (win_capture/win_mf, 默认平台值)", ""});
  cmd.parser.addArg(
      {"-vi", "", ArgType::Int, false, "视频设备索引 (默认 0, -1=无)", "0"});
  cmd.parser.addArg({"-ai", "", ArgType::Int, false,
                     "音频设备索引 (默认 0, -1=无)", "0"});
  cmd.parser.addArg({"-vname", "", ArgType::String, false,
                     "按名称子串选视频设备", ""});
  cmd.parser.addArg({"-aname", "", ArgType::String, false,
                     "按名称子串选音频设备", ""});
  cmd.parser.addArg(
      {"-no-audio", "", ArgType::Boolean, false, "不采集音频", ""});
  cmd.parser.addArg(
      {"-no-video", "", ArgType::Boolean, false, "不采集视频", ""});
  cmd.parser.addArg({"-record", "", ArgType::String, false,
                     "打开后自动录像到路径 (mp4/rtsp...)", ""});
  cmd.parser.addArg({"-shot-dir", "", ArgType::String, false,
                     "截图保存目录 (默认 <运行目录>/screenshots)", ""});

  cmd.run = [](const ParsedArgs& args) -> int {
    bool onlyList = args.getBool("list");
    // SDK 默认取平台值, -vsdk 可显式选择(如 win_mf 相机)
    VDeviceSdk vsdk = getDefaltVideoSdk();
    std::string vsdkArg = args.getString("vsdk", "");
    if (vsdkArg == "win_mf") vsdk = VDeviceSdk::win_mf;
    if (vsdkArg == "win_capture") vsdk = VDeviceSdk::win_capture;
    ADeviceSdk asdk = getDefaltAudioSdk();
    int vi = args.getInt("vi", 0);
    int ai = args.getInt("ai", 0);
    std::string vname = args.getString("vname", "");
    std::string aname = args.getString("aname", "");
    bool noAudio = args.getBool("no-audio") || ai < 0;
    bool noVideo = args.getBool("no-video") || vi < 0;
    std::string recordPath = args.getString("record", "");
    std::string shotDir = args.getString("shot-dir", "");
    if (shotDir.empty()) shotDir = getAvoxPath() + "/screenshots";
    // 取设备管理器
    IVideoManager* videoMgr = getVideoManager(vsdk);
    IAudioManager* audioMgr = getAudioManager(asdk);
    // 枚举视频设备
    int vCount = videoMgr ? videoMgr->getDeviceCount() : 0;
    printf("video devices (%s): %d\n", vSdkStr(vsdk), vCount);
    for (int i = 0; i < vCount; ++i) {
      IVideoSource* d = videoMgr->getDevice(i);
      printf("  [%d] %s\n", i, d ? d->getDeviceName() : "?");
    }
    // 枚举音频设备
    int aCount = audioMgr ? audioMgr->getDeviceCount() : 0;
    printf("audio devices (%s): %d\n", aSdkStr(asdk), aCount);
    for (int i = 0; i < aCount; ++i) {
      IAudioSource* d = audioMgr->getDevice(i);
      printf("  [%d] %s\n", i, d ? d->getDeviceName() : "?");
    }
    if (onlyList) return 0;
#ifndef _WIN32
    printf("device 采集预览需要 Windows (Win32 消息循环)\n");
    return 0;
#else
    // 选视频设备 (按名称子串优先, 否则用索引)
    IVideoSource* vsrc = nullptr;
    if (!noVideo) {
      int idx = vi;
      if (!vname.empty()) {
        int found = findVideoByName(videoMgr, vname);
        if (found >= 0) {
          idx = found;
          printf("视频按名称 '%s' 命中 [%d]\n", vname.c_str(), idx);
        } else {
          printf("视频按名称 '%s' 未命中, 用索引 %d\n", vname.c_str(), idx);
        }
      }
      if (videoMgr && idx >= 0 && idx < vCount) vsrc = videoMgr->getDevice(idx);
      if (!vsrc) {
        fprintf(stderr, "视频设备不可用 (idx=%d)\n", idx);
        return 1;
      }
    }
    // 选音频设备 (按名称子串优先, 否则用索引)
    IAudioSource* asrc = nullptr;
    if (!noAudio) {
      int idx = ai;
      if (!aname.empty()) {
        int found = findAudioByName(audioMgr, aname);
        if (found >= 0) {
          idx = found;
          printf("音频按名称 '%s' 命中 [%d]\n", aname.c_str(), idx);
        } else {
          printf("音频按名称 '%s' 未命中, 用索引 %d\n", aname.c_str(), idx);
        }
      }
      if (audioMgr && idx >= 0 && idx < aCount) asrc = audioMgr->getDevice(idx);
      if (!asrc) printf("  (音频设备不可用, 仅视频)\n");
    }
    // 创建 SourcePlayer + 配置窗口 (setSurface(nullptr) 自动创建 Vulkan 窗口)
    ISourcePlayer* sp = createDevicePlayer();
    if (vsrc) sp->setVideoSource(vsrc);
    if (asrc) sp->setAudioSource(asrc);
    ISurfaceRender* render = sp->getSurfaceRender();
    render->setVulkan(true);
    render->setSurface(nullptr);
    if (!sp->open()) {
      fprintf(stderr, "SourcePlayer open 失败\n");
      delete sp;
      return 1;
    }
    // 录像 muxer (sp 持有对象, 这里仅持指针表示是否在录; 不 delete)
    IMediaMuxer* muxer = nullptr;
    std::string recFile;
    std::string lastShotPath = "ready";  // 最近一次截图路径 (P 键, OSD 显示)
    int reopenCount = 0;                  // 重开设备次数 (A 键, OSD 显示)
    auto startRecord = [&](const std::string& path) {
      if (muxer) return;
      muxer = sp->getMuxer();
      muxer->setMuxerType(MuxerType::ffmpeg);
      if (muxer->open(path.c_str())) {
        recFile = path;
        printf("  record start: %s\n", path.c_str());
      } else {
        printf("  record open 失败: %s\n", path.c_str());
        muxer = nullptr;
      }
    };
    auto stopRecord = [&]() {
      if (!muxer) return;
      muxer->close();
      muxer = nullptr;
      printf("  record stop: %s\n", recFile.c_str());
    };
    if (!recordPath.empty()) startRecord(recordPath);
    // 截图: render->screenShot -> 保存 PNG
    ensureDir(shotDir);
    auto keyShot = [&]() {
      std::unique_ptr<IImageBuffer> buf(createImageBuffer());
      if (!buf || !render->screenShot(buf.get())) {
        printf("  screenshot 失败\n");
        return;
      }
      std::string path = shotDir + "/dev_shot_" + formatStamp_YMDHMS() + ".png";
      if (saveImagePath(path.c_str(), buf.get())) {
        lastShotPath = path;
        printf("  screenshot: %s\n", path.c_str());
      } else
        printf("  screenshot 保存失败: %s\n", path.c_str());
    };
    // 重开设备 (A 键, 借鉴 sourceplaytest): 关录像 -> 关 player -> 重开
    auto reopen = [&]() {
      stopRecord();
      sp->close();
      sp->open();
      ++reopenCount;
      if (!recordPath.empty()) startRecord(recordPath);
      printf("  device reopened\n");
    };
    // 交互式 OSD (借鉴 vkfonttest.cpp / cmdPlay): 窗口上叠加「键→动作→当前值」表,
    // 键盘实时控制, 人工验证 ISourcePlayer / ISurfaceRender / IMediaMuxer。
    // ISourcePlayer 无 getFps/getRate/getPosition/seek/speed, 故 OSD 表精简,
    // 聚焦 状态/截图录屏/设备 三块。
#ifdef AVOX_ENABLE_FREETYPE
    IFontLayer* fontLayer = nullptr;
    IGeometryLayer* geoLayer = nullptr;
    bool osdVisible = true;
    // OSD 颜色表 (C 键循环), 文本与几何装饰共用此表
    struct OsdColor {
      float r, g, b;
      const char* name;
    };
    static const OsdColor kOsdColors[] = {
        {0.2f, 0.6f, 1.0f, "亮蓝"}, {0.0f, 1.0f, 0.4f, "绿"},
        {1.0f, 0.8f, 0.0f, "橙"},   {1.0f, 0.3f, 0.3f, "红"},
        {0.8f, 0.4f, 1.0f, "紫"},   {1.0f, 1.0f, 1.0f, "白"},
    };
    const int kOsdColorCount = sizeof(kOsdColors) / sizeof(kOsdColors[0]);
    int colorIdx = 1;  // 默认绿 (与 cmdPlay 一致)
    // 初始化/恢复 13 行字体排版 (分块: 状态/截图录屏/设备)
    auto setupFontLayout = [&]() {
      if (!fontLayer) return;
      fontLayer->setFont("simhei.ttf", 32);
      const auto& c = kOsdColors[colorIdx];
      fontLayer->setColor(c.r, c.g, c.b, 0.0f);
      const float kY0 = 0.02f;
      const float kStep = 0.032f;
      for (int i = 0; i < 13; ++i) {
        FontLayout l = {};
        l.alignment.horizontal = HAlignType::left;
        l.alignment.vertical = VAlignType::top;
        l.x = 0.02f;
        l.y = kY0 + i * kStep;
        l.width = 0.9f;
        l.height = kStep;
        fontLayer->updateLayout(i, l);
      }
    };
    // 刷新全部 OSD 行 (每 ~250ms 或按键后调用; 只 drawText, 廉价)
    auto refreshOsd = [&]() {
      if (!fontLayer) return;
      char row[400];
      // ===== 状态 =====
      fontLayer->setTextLayout(0);
      fontLayer->drawText("状态");
      // 1: 视频/音频 设备名
      snprintf(row, sizeof(row), "[状态] 视频: %s | 音频: %s",
               vsrc ? vsrc->getDeviceName() : "(none)",
               asrc ? asrc->getDeviceName() : "(none)");
      fontLayer->setTextLayout(1);
      fontLayer->drawText(row);
      // 2: player 状态
      snprintf(row, sizeof(row), "[状态] player: %s",
               getPlayerStateStr(sp->getState()));
      fontLayer->setTextLayout(2);
      fontLayer->drawText(row);
      // 3: 视频流信息 (ready 后才有, 否则显示 -)
      {
        ISourceInfo* info = sp->getSourceInfo();
        if (info && info->videoSize() > 0) {
          VTrackDesc vdesc = info->getVideoDesc(0);
          std::string vdescStr;
          string_format(vdescStr, vdesc);
          snprintf(row, sizeof(row), "[状态] 视频流: %s", vdescStr.c_str());
        } else {
          snprintf(row, sizeof(row), "[状态] 视频流: -");
        }
        fontLayer->setTextLayout(3);
        fontLayer->drawText(row);
      }
      // 4: 音频流信息 (ready 后才有, 否则显示 -)
      {
        ISourceInfo* info = sp->getSourceInfo();
        if (info && info->audioSize() > 0) {
          ATrackDesc adesc = info->getAudioDesc(0);
          std::string adescStr;
          string_format(adescStr, adesc);
          snprintf(row, sizeof(row), "[状态] 音频流: %s", adescStr.c_str());
        } else {
          snprintf(row, sizeof(row), "[状态] 音频流: -");
        }
        fontLayer->setTextLayout(4);
        fontLayer->drawText(row);
      }
      // ===== 截图/录屏 =====
      fontLayer->setTextLayout(5);
      fontLayer->drawText("截图/录屏");
      // 6: 截图 -> 最近截图路径
      snprintf(row, sizeof(row), "[P] 截图 : %s", lastShotPath.c_str());
      fontLayer->setTextLayout(6);
      fontLayer->drawText(row);
      // 7: 录像 (R) 开/关
      if (muxer) {
        snprintf(row, sizeof(row), "[R] 录像 : [REC] %s", recFile.c_str());
      } else {
        snprintf(row, sizeof(row), "[R] 录像 : 关");
      }
      fontLayer->setTextLayout(7);
      fontLayer->drawText(row);
      // ===== 设备 =====
      fontLayer->setTextLayout(8);
      fontLayer->drawText("设备");
      // 9: 重开设备 (A)
      snprintf(row, sizeof(row), "[A] 重开设备 : %d 次", reopenCount);
      fontLayer->setTextLayout(9);
      fontLayer->drawText(row);
      // 10: 隐藏OSD (O)
      fontLayer->setTextLayout(10);
      fontLayer->drawText("[O] 隐藏OSD");
      // 11: 颜色 (C)
      snprintf(row, sizeof(row), "[C] 颜色 : %s", kOsdColors[colorIdx].name);
      fontLayer->setTextLayout(11);
      fontLayer->drawText(row);
      // 12: quit
      fontLayer->setTextLayout(12);
      fontLayer->drawText("[Q] quit");
    };
    // 几何装饰: 标题行左右实心小圆, quit 行矩形框 (与 cmdPlay 一致)
    // 1汉字 ≈ fontSize/帧宽 ≈ 32/1920 ≈ 0.017 归一化宽度
    auto drawGeoDecor = [&]() {
      if (!geoLayer) return;
      const auto& gc = kOsdColors[colorIdx];
      geoLayer->setColor(gc.r, gc.g, gc.b);
      geoLayer->setThreshold(0.65f);
      geoLayer->clear();
      const float kY0 = 0.02f;
      const float kStep = 0.032f;
      const float kTextX = 0.02f;
      const float kDotR = 12.0f;  // 圆半径(帧px)
      // 标题行: (行号, 汉字数) — 估算右圆位置
      struct TitleInfo {
        int row;
        int charUnits;
      };
      const TitleInfo titles[] = {
          {0, 2},  // 状态
          {5, 5},  // 截图/录屏 (/算半字)
          {8, 2},  // 设备
      };
      for (const auto& t : titles) {
        float cy = kY0 + t.row * kStep + kStep * 0.5f;
        float leftX = kTextX - 0.010f;
        float rightX = kTextX + t.charUnits * 0.017f + 0.010f;
        geoLayer->drawCircle(leftX, cy, kDotR, true);
        geoLayer->drawCircle(rightX, cy, kDotR, true);
      }
      // quit 行(12): 矩形框包住 "[Q] quit" (8 ASCII 半字宽)
      float quitY = kY0 + 12 * kStep;
      float quitX0 = kTextX - 0.006f;
      float quitX1 = kTextX + 8 * 0.0085f + 0.006f;
      float quitY0 = quitY + 0.004f;
      float quitY1 = quitY + kStep + 0.006f;
      geoLayer->drawRect(quitX0, quitY0, quitX1, quitY1);
    };
    // OSD + 几何 开关 (O 键): 同时显隐 OSD 字体和几何叠加
    auto toggleOsd = [&]() {
      if (osdVisible) {
        disableRenderFont(render);
        fontLayer = nullptr;
        osdVisible = false;
        if (geoLayer) {
          disableRenderGeometry(render);
          geoLayer = nullptr;
        }
        printf("  OSD off (press O to show)\n");
      } else {
        fontLayer = enableRenderFont(render);
        if (fontLayer) {
          setupFontLayout();
          refreshOsd();
        }
        osdVisible = true;
        geoLayer = enableRenderGeometry(render);
        drawGeoDecor();
        printf("  OSD on\n");
      }
    };
    // 切换 OSD 颜色 (C 键): 文本与几何装饰共用 colorIdx, 同步重画几何
    auto cycleColor = [&]() {
      if (!fontLayer) return;
      colorIdx = (colorIdx + 1) % kOsdColorCount;
      const auto& c = kOsdColors[colorIdx];
      fontLayer->setColor(c.r, c.g, c.b, 0.0f);
      if (geoLayer) drawGeoDecor();
      printf("  OSD color: %s\n", c.name);
    };
    // 初始化 OSD 字体 + 几何 (open 成功后, 窗口已建)
    fontLayer = enableRenderFont(render);
    if (fontLayer) {
      setupFontLayout();
      refreshOsd();
    }
    geoLayer = enableRenderGeometry(render);
    drawGeoDecor();
#endif  // AVOX_ENABLE_FREETYPE
    // 配置打印
    printf("avox_cli device\n");
    printf("  video: %s\n", vsrc ? vsrc->getDeviceName() : "(none)");
    printf("  audio: %s\n", asrc ? asrc->getDeviceName() : "(none)");
    printf("  record: %s\n",
           muxer ? recFile.c_str() : (recordPath.empty() ? "(off)" : "(failed)"));
    printf("  shot-dir: %s\n", shotDir.c_str());
    printf("  keys: P=截图 R=录像(开/关) A=重开设备 O=OSD C=颜色 Q=quit\n");
    // 运行循环 (Ctrl+C / 关窗口 / Q / ESC 退出)
    gCmdRunning = true;
    std::signal(SIGINT, cmdSignalHandler);
    std::signal(SIGTERM, cmdSignalHandler);
#ifdef AVOX_ENABLE_FREETYPE
    auto lastOsd = std::chrono::steady_clock::now();  // OSD 表 ~250ms 刷新计时
#endif
    while (gCmdRunning) {
      // 窗口被关 (点X/Alt+F4) -> 退出
      if (render->getSurface() && !IsWindow((HWND)render->getSurface())) {
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
            case 'P':  // 截图
              keyShot();
              break;
            case 'R': {  // 录像 开/关 (默认存到 <运行目录>/records)
              if (muxer) {
                stopRecord();
              } else {
                std::string recDir = getAvoxPath() + "/records";
                ensureDir(recDir);
                std::string path = recDir + "/dev_record_" + formatStamp_YMDHMS() + ".mp4";
                startRecord(path.c_str());
              }
              break;
            }
            case 'A':  // 重开设备
              reopen();
              break;
            case 'O':  // 切换 OSD+几何
#ifdef AVOX_ENABLE_FREETYPE
              toggleOsd();
#else
              printf("  OSD not available\n");
#endif
              break;
            case 'C':  // OSD 颜色循环
#ifdef AVOX_ENABLE_FREETYPE
              cycleColor();
#endif
              break;
            default:
              break;
          }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
#ifdef AVOX_ENABLE_FREETYPE
      // OSD 表 ~250ms 刷新: 状态/截图/录像 等行实时跟随按键变化
      if (fontLayer &&
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - lastOsd)
                  .count() >= 250) {
        lastOsd = std::chrono::steady_clock::now();
        refreshOsd();
      }
#endif
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    stopRecord();
    sp->close();
    delete sp;
    return 0;
#endif  // _WIN32
  };
  return cmd;
}

}
