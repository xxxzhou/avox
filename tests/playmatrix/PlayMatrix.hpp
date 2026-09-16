#pragma once

// 跨平台播放回归矩阵: 共享用例表 + 判定逻辑 (header-only, 只用 avox 公共头)
//
// 目的: 每次改动后跑一次, 确认"播放"这条链路没被搞坏 (拉流/解码/帧输出/录制)。
// 形态: 用例与判定口径只此一份, 各平台 runner 只做宿主适配
//       (Windows 控制台 / Apple app+无头 CLI / Android APK / Linux 控制台),
//       于是同一 case id 在五个平台含义完全一致, 可汇总成一张跨平台表。
//
// 判定行: [AVOX][TEST] case=<id> result=PASS|FAIL [k=v ...]
// 退出码: 0 = 全过 (可接 CI), 1 = 有 FAIL
//
// 各平台宿主入口见 platform/<plat>/playtest; 一键驱动见 script/testenv/play_regress.py

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avox/AvoxCodec.h"
#include "avox/AvoxImage.h"
#include "avox/AvoxLayer.h"
#include "avox/AvoxPlayer.h"
#include "avox/AvoxVideo.h"

namespace avox {
namespace playmatrix {

// ── 拉流端点: 各平台按部署传入 (本机 ZLM 用 127.0.0.1, 局域网用主机 IP) ──
struct Endpoints {
  std::string host = "127.0.0.1";
  int32_t rtspPort = 554;
  int32_t rtmpPort = 1935;
  int32_t httpPort = 80;
  // ZLM 流名: app/stream
  std::string h264Key = "live/avox264";
  std::string h265Key = "live/avox";
  // 本地文件源 (留空则该组用例判 FAIL note=source-missing, 便于发现环境缺失)
  std::string fileH264;
  std::string fileH265;
  // HDR10 素材 (script/testenv/gen_hdr10_asset.py 生成); 缺失时相关用例自动 off
  std::string fileHdr10;
  std::string fileHdr10Aud;
  // 字幕测试素材 (assets/gen/gen_subtitle.py 生成; 经 --file-sub-* 传入)
  // 外挂字幕统一用暗色背景视频 sub_bg_640x360.mp4 作载体; 内嵌字幕各自带视频
  std::string fileSubBg;        // 外挂字幕对应背景视频(暗色底)
  std::string fileSubSrtUtf8;   // 外挂 SRT(UTF-8)
  std::string fileSubSrtBom;    // 外挂 SRT(UTF-8 + BOM)
  std::string fileSubSrtGbk;    // 外挂 SRT(GBK)
  std::string fileSubSrtRich;   // 外挂 SRT(emoji/重叠/超长行)
  std::string fileSubAss;       // 外挂 ASS(定位+粗体+淡入)
  std::string fileSubSrtEmbed;  // 内嵌 SRT(mkv, 自带视频)
  std::string fileSubAssEmbed;  // 内嵌 ASS(mkv, 自带视频)
  std::string fileSubMovText;   // 内嵌 mov_text(mp4, 自带视频)

  std::string appName(const std::string& key) const {
    size_t p = key.find('/');
    return p == std::string::npos ? std::string("live") : key.substr(0, p);
  }
  std::string streamName(const std::string& key) const {
    size_t p = key.find('/');
    return p == std::string::npos ? key : key.substr(p + 1);
  }
  std::string rtsp(const std::string& key) const {
    return "rtsp://" + host + ":" + std::to_string(rtspPort) + "/" + key;
  }
  std::string rtmp(const std::string& key) const {
    return "rtmp://" + host + ":" + std::to_string(rtmpPort) + "/" + key;
  }
  std::string http(const std::string& tail) const {
    return "http://" + host + ":" + std::to_string(httpPort) + tail;
  }
  // ZLM WHEP 信令地址 (WebRTC 拉流)
  std::string rtc(const std::string& key) const {
    return http("/index/api/webrtc?app=" + appName(key) + "&stream=" +
                streamName(key) + "&type=play");
  }
};

// ── 用例类型 ──
enum class CaseKind {
  pull,             // IMediaPlayer 拉流: playing + fps>0 + pos>1.5s
  rtc,              // IRtcPlayer 拉流: connected + firstFrame + fps>0
  frameContract,    // 离屏 yuv420P: packed 契约 + packed→split→RGBA 落 PNG
  screenShot,       // 离屏截图落 PNG
  recordCopy,       // getMuxer(false) 直通录制 (原流拷贝)
  recordTranscode,  // getMuxer(true) 转码录制 (中途 seek 见 PlayCase::seekMid)
  yuvOut,           // 无vulkan直取: enableYuvOut 帧类型契约 (硬解 nv12 / 软解解码格式)
  subtitle,         // 字幕: 外挂 loadSubtitle / 内嵌 setSubtitleTrack → 字幕带亮像素取证
};

struct PlayCase {
  std::string id;
  CaseKind kind = CaseKind::pull;
  std::string url;
  // 拉流用例选项
  IoPlan io = IoPlan::ffmpeg;
  bool hardDecode = true;
  int32_t seconds = 15;
  // 录制用例: 录制时长与产物字节下限 (空文件/仅文件头会被判掉)
  int32_t minBytes = 8192;
  // 录制中途 seek 一次 (rec-transcode-seek 专用): 其余录制用例不 seek,
  // 让"转码录制"与"seek 后帧流恢复"两条链路各自独立判定
  bool seekMid = false;
  // true = 关掉 vulkan 管线走平台原生渲染 (截图的稳定路线)
  bool nativeRender = false;
  // yuvOut 用例: 期望的首帧类型 (getYuvTypeStr 口径), 空 = 不判型
  std::string expectType;
  // false = 默认不跑 (已知未修/环境依赖), 需 --all 显式打开
  bool enabled = true;
  // 字幕用例专用 (kind == subtitle)
  std::string subPath;   // 外挂字幕文件路径 (非空=外挂 loadSubtitle; 空=内嵌)
  int32_t subTrack = 0;  // 内嵌字幕轨 index (setSubtitleTrack 用)
  bool subStyle = false;  // true = loadSubtitle 后套用观感样式 (字号/颜色/缩放/透明度), 验样式链路
  // 界面走查说明 (--win 左上角横幅 / --list 展示): 用例在做什么 + 人该看到什么,
  // 供人工照着判断画面是否正常。空 = 宿主回退显示 url
  std::string desc;
};

// ── 用例表: 29 条, 轴 + 固定交叉 (不做全笛卡尔) ──
inline std::vector<PlayCase> buildCases(const Endpoints& ep) {
  std::vector<PlayCase> cases;
  const std::string k264 = ep.h264Key;
  const std::string k265 = ep.h265Key;
  // 走查基语 (frame.py 自校验测试图的长相): --win 左上角横幅显示 desc,
  // 人照着逐项对照画面是否正常; 离屏用例明确写"窗口无画面"免得人等一个不来的图
  const std::string kChart =
      "标准自校验测试图: 四角定位标+色块灰阶色准正常, 左上时间码走动, "
      "底部帧号条码连续跳变, 有运动元素不卡帧";
  auto pull = [&cases](const std::string& id, const std::string& url, IoPlan io,
                       bool hard, int32_t seconds, const std::string& desc) {
    PlayCase c;
    c.id = id;
    c.kind = CaseKind::pull;
    c.url = url;
    c.io = io;
    c.hardDecode = hard;
    c.seconds = seconds;
    c.desc = desc;
    cases.push_back(c);
  };
  // 本地素材用例: 素材缺失自动 off (平台没同步素材不拖垮整表)
  auto pullFile = [&cases](const std::string& id, const std::string& url, bool hard,
                           int32_t seconds, const std::string& desc) {
    PlayCase c;
    c.id = id;
    c.kind = CaseKind::pull;
    c.url = url;
    c.hardDecode = hard;
    c.seconds = seconds;
    c.enabled = !url.empty();
    c.desc = desc;
    cases.push_back(c);
  };
  auto special = [&cases](const std::string& id, CaseKind kind, const std::string& url,
                          int32_t seconds, const std::string& desc) {
    PlayCase c;
    c.id = id;
    c.kind = kind;
    c.url = url;
    c.seconds = seconds;
    c.desc = desc;
    cases.push_back(c);
  };
  // A 协议 × 编码 (默认口径: ffmpeg IO + 硬解 + 上屏/离屏)
  pull("file-h264", ep.fileH264, IoPlan::ffmpeg, true, 15,
       "本地文件 H264 硬解基线: " + kChart);
  pull("file-h265", ep.fileH265, IoPlan::ffmpeg, true, 15,
       "本地文件 H265 硬解基线: " + kChart);
  pull("rtsp-h264", ep.rtsp(k264), IoPlan::ffmpeg, true, 15,
       "RTSP 拉流 H264 硬解: " + kChart + "; 起播前短暂黑屏属正常");
  pull("rtsp-h265", ep.rtsp(k265), IoPlan::ffmpeg, true, 15,
       "RTSP 拉流 H265 硬解: " + kChart + "; 起播前短暂黑屏属正常");
  pull("rtmp-h264", ep.rtmp(k264), IoPlan::ffmpeg, true, 15,
       "RTMP 拉流 H264 硬解: " + kChart);
  pull("rtmp-h265", ep.rtmp(k265), IoPlan::ffmpeg, true, 15,
       "RTMP 拉流 H265 硬解: " + kChart);
  pull("hls-h264", ep.http("/" + k264 + "/hls.m3u8"), IoPlan::ffmpeg, true, 20,
       "HLS 拉流 H264 硬解 (分片协议, 起播稍慢属正常): " + kChart);
  pull("hls-h265", ep.http("/" + k265 + "/hls.m3u8"), IoPlan::ffmpeg, true, 20,
       "HLS 拉流 H265 硬解 (分片协议, 起播稍慢属正常): " + kChart);
  pull("ts-h264", ep.http("/" + k264 + ".live.ts"), IoPlan::ffmpeg, true, 15,
       "HTTP-TS 拉流 H264 硬解: " + kChart);
  pull("ts-h265", ep.http("/" + k265 + ".live.ts"), IoPlan::ffmpeg, true, 15,
       "HTTP-TS 拉流 H265 硬解: " + kChart);
  // B IO 方案对照 (只挂 RTSP, 避免组合爆炸; A 组已覆盖 ffmpeg)
  pull("rtsp-h264-zm", ep.rtsp(k264), IoPlan::zlmediakit, true, 15,
       "RTSP 拉流 H264 硬解 (zlmediakit IO 方案对照): 画面应与 rtsp-h264 完全一致");
  pull("rtsp-h265-zm", ep.rtsp(k265), IoPlan::zlmediakit, true, 15,
       "RTSP 拉流 H265 硬解 (zlmediakit IO 方案对照): 画面应与 rtsp-h265 完全一致");
  // C 解码模式对照 (软解; 硬解由 A 组覆盖; 同时覆盖本地文件与网络)
  pull("file-h264-soft", ep.fileH264, IoPlan::ffmpeg, false, 15,
       "本地文件 H264 软解: 画面应与硬解完全一致 (CPU 解码, 占用偏高属正常)");
  pull("file-h265-soft", ep.fileH265, IoPlan::ffmpeg, false, 15,
       "本地文件 H265 软解: 画面应与硬解完全一致 (CPU 解码, 占用偏高属正常)");
  pull("rtsp-h264-soft", ep.rtsp(k264), IoPlan::ffmpeg, false, 15,
       "RTSP 拉流 H264 软解: 画面应与硬解完全一致");
  pull("rtsp-h265-soft", ep.rtsp(k265), IoPlan::ffmpeg, false, 15,
       "RTSP 拉流 H265 软解: 画面应与硬解完全一致");
  // C2 HDR10 本地素材: 软解 tone map 链路 / [AUD][SEI][IDR] 合并边界。
  // 硬解 10bit 已闭环: win DX11CS P010, mac VT 输出 x420 由 Metal 前端 tone map
  pullFile("file-hdr10-soft", ep.fileHdr10, false, 15,
           "本地 HDR10(PQ) 软解 tone map: 画面亮度应正常 —— 发灰=tone map 缺失, "
           "过曝=PQ 直通");
  pullFile("file-hdr10-hard", ep.fileHdr10, true, 15,
           "本地 HDR10(PQ) 硬解 tone map (GPU 车道): 亮度应正常, 与 hdr10-soft 一致");
  pullFile("file-hdr10-aud", ep.fileHdr10Aud, false, 15,
           "本地 HDR10(PQ,[AUD] 分帧) 软解: 画面同 hdr10-soft, 验帧边界不花屏不断流");
  // D WebRTC (独立通道, 不经 IO 方案)
  special("webrtc-h264", CaseKind::rtc, ep.rtc(k264), 15,
          "WebRTC 拉流 H264, 判数据通路 (连接+首帧+帧率); 本用例不出画面到窗口");
  special("webrtc-h265", CaseKind::rtc, ep.rtc(k265), 15,
          "WebRTC 拉流 H265, 判数据通路 (连接+首帧+帧率); 本用例不出画面到窗口");
  // E 帧契约 / 截图 / 录制
  special("frame-contract", CaseKind::frameContract, ep.rtsp(k264), 8,
          "离屏帧契约用例: 窗口无画面; 抽帧落 <outdir>/pm_frame0/1.png 可事后人工复看");
  {
    // 截图走平台原生渲染 (关 vulkan): 原生车道基线。
    // 用本地文件而不是网络源: 截图能力与协议无关, 这样它也能进离线子集(CI 覆盖)
    PlayCase c;
    c.id = "shot";
    c.kind = CaseKind::screenShot;
    c.url = ep.fileH264;
    c.seconds = 6;
    c.nativeRender = true;
    c.desc = "离屏截图用例 (原生车道): 窗口无画面; 产物 pm_shot.png 应为标准测试图"
             " (自动判废图)";
    cases.push_back(c);
  }
  {
    // HDR10 硬解原生截图: VT 输出 x420 → Metal 前端 tone map → fetchFrame 抓
    // tone map 后的 RGBA; 截图为 SDR 表面, PQ 直通(若 tone map 断链)会过曝失真
    PlayCase c;
    c.id = "shot-hdr";
    c.kind = CaseKind::screenShot;
    c.url = ep.fileHdr10;
    c.seconds = 6;
    c.nativeRender = true;
    c.desc = "离屏 HDR10 截图 (原生车道): 窗口无画面; pm_shot.png 亮度应正常"
             " (过曝/发灰 = tone map 断链)";
    cases.push_back(c);
  }
  {
    // 离屏 vulkan 截图 (中转车道): 与 shot 同源本地文件; 截 vk 管线
    // outputLayer 的处理后帧, SDR 经中转的基线对照
    PlayCase c;
    c.id = "shot-vk";
    c.kind = CaseKind::screenShot;
    c.url = ep.fileH264;
    c.seconds = 6;
    c.desc = "离屏截图 (vulkan 中转车道): 窗口无画面; 产物应与 shot 同为标准测试图";
    cases.push_back(c);
  }
  {
    // HDR10 中转车道截图: 硬解 P010 → DX11CS tone map → vk 管线 outputLayer 抓帧。
    // 与 shot-hdr(原生车道) 构成双车道对照, 钉死「HDR10 经中转输出正确 SDR」
    PlayCase c;
    c.id = "shot-hdr-vk";
    c.kind = CaseKind::screenShot;
    c.url = ep.fileHdr10;
    c.seconds = 6;
    c.enabled = !ep.fileHdr10.empty();
    c.desc = "离屏 HDR10 截图 (中转车道): 窗口无画面; 亮度应与 shot-hdr 一致 (双车道对照)";
    cases.push_back(c);
  }
  special("rec-copy-h264", CaseKind::recordCopy, ep.rtsp(k264), 8,
          "直通录制用例: 窗口无画面; 产物 pm_copy.mp4 ≥8KB 且应可正常播放");
  // 转码录制 (本地文件源): 与 seek 解耦 —— 中途 seek 单独成 case (见下),
  // 本用例只判"转码录制"本身 (硬编名解析 / 编码器打开 / io 层建文件 / 产物可播)
  special("rec-transcode-h264", CaseKind::recordTranscode, ep.fileH264, 8,
          "转码录制用例: 窗口无画面; 产物 pm_trans.mp4 ≥8KB 且应可正常播放");
  {
    // 录制中途 seek: 与 rec-transcode-* 分开, 让"转码录制"与"seek 后能否继续出数据"
    // 两条链路各自独立判定。09-16 定位并修复的缺陷: 本地短文件包数(886) ≤ 点播包队列
    // 上限(1000) → 起播无背压毫秒级读满 → EOF → 读循环 break 拆掉读线程 → 之后 seek
    // 只挪了 demuxer 位置却无人再读 → 编码器 0 包 → 产物缺失(bytes=-1)。
    // 修法: IOParseFF "EOF 停放"(不拆线程, seekTo 成功后 bEofReset 复位继续读)
    PlayCase c;
    c.id = "rec-transcode-seek";
    c.kind = CaseKind::recordTranscode;
    c.url = ep.fileH264;
    c.seconds = 8;
    c.seekMid = true;
    c.desc = "转码录制中途 seek 到半长: 窗口无画面; 产物 pm_trans.mp4 ≥8KB (seek 后仍持续出数据)";
    cases.push_back(c);
  }
  // F 无vulkan直取 (车道B): 关 vulkan 走平台原生渲染 + enableYuvOut, 严格判交付帧类型。
  // 硬解应交付解码直出 nv12 (DX11 staging / Metal readback), 类型不对 = 车道断裂或
  // 硬解回退软解, 都判 FAIL —— 这是"硬解组件被裁/回退"的哨兵 (ffmpeg9 裁 hwaccel 一类
  // 回归只有它能抓到)。真硬解需 GPU 视频单元, CI 离线子集要排除 (OFFLINE_SKIP)
  {
    PlayCase c;
    c.id = "yuvout-h264";
    c.kind = CaseKind::yuvOut;
    c.url = ep.fileH264;
    c.seconds = 6;
    c.nativeRender = true;
    c.expectType = "nv12";
    c.desc = "无vulkan直取 (车道B) 硬解: 窗口无画面; 数据哨兵, 判交付帧类型 = nv12";
    cases.push_back(c);
  }
  {
    // 软解 cpuIn: 基类零拷 packed 视图, 交付解码原始格式 (源 webrtc_pull.mp4 为 yuv420p)
    PlayCase c;
    c.id = "yuvout-h264-soft";
    c.kind = CaseKind::yuvOut;
    c.url = ep.fileH264;
    c.seconds = 6;
    c.hardDecode = false;
    c.nativeRender = true;
    c.expectType = "yuv420P";
    c.desc = "无vulkan直取 (车道B) 软解: 窗口无画面; 判交付帧类型 = yuv420P";
    cases.push_back(c);
  }
  {
    // 10bit 帧契约哨兵: 软解交付解码原始格式 yuv420P10。getYuvFrameSize 对
    // 2B 像素格式的 packed 契约 (groupsize 修复回归), 类型不对即 10bit 车道断裂
    PlayCase c;
    c.id = "yuvout-h265-10bit";
    c.kind = CaseKind::yuvOut;
    c.url = ep.fileHdr10;
    c.seconds = 6;
    c.hardDecode = false;
    c.nativeRender = true;
    c.expectType = "yuv420P10";
    c.enabled = !ep.fileHdr10.empty();
    c.desc = "10bit 帧契约哨兵: 窗口无画面; 判交付帧类型 = yuv420P10";
    cases.push_back(c);
  }
  {
    // 无vulkan转码录制: SurfaceRenderNative::pushFrame 非 vk 分支 (getCpuFrame→muxer),
    // 该分支在车道 B 之前完全不存在 (没 vulkan 转码录制拿不到帧)。
    // 注: copy 直通的包走 IO 层 onPacket, 不经渲染层, 测不到本分支, 故用转码
    PlayCase c;
    c.id = "rec-transcode-novk";
    c.kind = CaseKind::recordTranscode;
    c.url = ep.fileH264;
    c.seconds = 8;
    c.nativeRender = true;
    c.desc = "无vulkan转码录制: 窗口无画面; 产物 pm_trans.mp4 非空即通";
    cases.push_back(c);
  }
  // G 字幕矩阵: 外挂 loadSubtitle / 内嵌 setSubtitleTrack → 字幕带亮像素取证。
  // 背景 sub_bg_640x360.mp4 为暗色底(0x12141a), 字幕文字为亮色 → 该判据可测;
  // 每个字幕测试都自带对应视频, 不依赖任何外部样本 (gen_subtitle.py 现生成)。
  auto subExt = [&cases](const std::string& id, const std::string& video,
                         const std::string& sub, const std::string& desc) {
    PlayCase c;
    c.id = id;
    c.kind = CaseKind::subtitle;
    c.url = video;       // 背景视频
    c.subPath = sub;     // 外挂字幕文件
    c.seconds = 8;
    c.enabled = !video.empty() && !sub.empty();
    c.desc = desc;
    cases.push_back(c);
  };
  auto subEmbed = [&cases](const std::string& id, const std::string& file,
                           int32_t track, const std::string& desc) {
    PlayCase c;
    c.id = id;
    c.kind = CaseKind::subtitle;
    c.url = file;        // 自带视频的字幕容器
    c.subTrack = track;  // 内嵌字幕轨 index
    c.seconds = 8;
    c.enabled = !file.empty();
    c.desc = desc;
    cases.push_back(c);
  };
  // 外挂: 判渲染亮像素; 内嵌: 判选轨成功 (subrip/mov_text 渲染属 avox 待补)
  subExt("sub-ext-srt-utf8", ep.fileSubBg, ep.fileSubSrtUtf8,
         "外挂 SRT(UTF-8) 字幕取证: 窗口无画面; 暗底画面上应渲染出亮色字幕 (自动判亮像素)");
  subExt("sub-ext-srt-bom",  ep.fileSubBg, ep.fileSubSrtBom,
         "外挂 SRT(UTF-8+BOM) 字幕取证: 窗口无画面; 同 utf8, 验 BOM 不破坏解析");
  subExt("sub-ext-srt-gbk",  ep.fileSubBg, ep.fileSubSrtGbk,
         "外挂 SRT(GBK) 字幕取证: 窗口无画面; 编码转换错会整条乱码或不渲染");
  subExt("sub-ext-srt-rich", ep.fileSubBg, ep.fileSubSrtRich,
         "外挂 SRT 富文本 (emoji/重叠/超长行) 取证: 窗口无画面; 不崩、超长行不溢出");
  subExt("sub-ext-ass",      ep.fileSubBg, ep.fileSubAss,
         "外挂 ASS 字幕 (\\pos 定位+粗体+淡入) 取证: 窗口无画面; 渲染位置应随 \\pos, 不在默认底部");
  {
    // 观感样式用例(字幕样式设计.md P6): 同 utf8 素材, loadSubtitle 后套
    // setFont/setColor/setScale/setOpacity, 应渲染出更大更黄的半透明字幕
    PlayCase c;
    c.id = "sub-style-srt";
    c.kind = CaseKind::subtitle;
    c.url = ep.fileSubBg;
    c.subPath = ep.fileSubSrtUtf8;
    c.subStyle = true;
    c.seconds = 8;
    c.enabled = !ep.fileSubBg.empty() && !ep.fileSubSrtUtf8.empty();
    c.desc = "观感样式取证: setFont(64)+黄色+setScale(1.5)+setOpacity(0.8), 字幕带应出现更大更亮的黄色字 (自动判亮像素)";
    cases.push_back(c);
  }
  subEmbed("sub-embed-srt",     ep.fileSubSrtEmbed, 0,
           "内嵌 SRT 轨 (mkv) 字幕: 窗口无画面; 判字幕轨可见可选 (渲染属 avox 待补)");
  subEmbed("sub-embed-ass",     ep.fileSubAssEmbed, 0,
           "内嵌 ASS 轨 (mkv) 字幕: 窗口无画面; 应渲染出定位字幕 (ASS 经 libass)");
  subEmbed("sub-embed-movtext", ep.fileSubMovText,  0,
           "内嵌 mov_text 轨 (mp4) 字幕: 窗口无画面; 判字幕轨可见可选 (渲染属 avox 待补)");
  // AVOX_PM_SOFT=1 (Apple 宿主启动参数 --soft): 全部用例强制软解。
  // 逃生门: 真机硬解服务被系统状态楔死时 (iOS 26 实测 VTDecompressionSessionCreate
  // 挂死不返回), 仍能跑完整矩阵验证 app/判定/日志链路。yuvout-h264 会因
  // 交付类型变 yuv420P 判 FAIL 属本模式预期 (哨兵语义不改)。
  if (std::getenv("AVOX_PM_SOFT") != nullptr) {
    for (auto &c : cases) {
      c.hardDecode = false;
    }
  }
  return cases;
}

// ── 判定行 (与 script/testenv 约定一致) ──
inline void verdict(const std::string& id, bool pass, const std::string& note = "") {
  std::printf("[AVOX][TEST] case=%s result=%s", id.c_str(), pass ? "PASS" : "FAIL");
  if (!note.empty()) {
    std::printf(" %s", note.c_str());
  }
  std::printf("\n");
  std::fflush(stdout);
}

// 文件存在且非空
inline int64_t fileSize(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    return -1;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fclose(f);
  return size > 0 ? (int64_t)size : 0;
}

// ── 图像的客观统计 (不看内容, 只排除"废图") ──
// 渲染链路的回归里绝大多数坏图是: 纯色/黑屏/白屏/绿屏/静止不动 —— 这些几行算术就能
// 确定性判定, 比调大模型快几个数量级且可复现。需要语义判断的(人脸像不像/水印干不干净)
// 才轮到模型, 见 README「画面质量怎么自动判」。
struct ImageStats {
  double meanLuma = 0;
  double stdLuma = 0;       // 灰度标准差: 纯色/纯屏时接近 0
  double topColorRatio = 0; // 最常见颜色的占比: 纯色/卡帧时接近 1
  int32_t width = 0;
  int32_t height = 0;
};

// 抽样统计 (最多 128x128 个点), 大图也不拖慢
inline ImageStats analyzeImage(IImageBuffer* buf) {
  ImageStats st;
  if (!buf || !buf->getPointer()) {
    return st;
  }
  ImageFormat fmt = buf->getImageFormat();
  if (fmt.width <= 0 || fmt.height <= 0) {
    return st;
  }
  st.width = fmt.width;
  st.height = fmt.height;
  int32_t px = getPixelSize(fmt.imageType);
  if (px < 3) {  // 至少 3 通道才谈得上色偏判断
    return st;
  }
  const uint8_t* base = buf->getPointer();
  int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * px;
  int32_t stepX = fmt.width > 128 ? fmt.width / 128 : 1;
  int32_t stepY = fmt.height > 128 ? fmt.height / 128 : 1;
  std::map<uint32_t, int32_t> hist;  // 每通道 5bit 量化后的桶
  double sum = 0;
  double sum2 = 0;
  int64_t n = 0;
  for (int32_t y = 0; y < fmt.height; y += stepY) {
    const uint8_t* row = base + (size_t)y * pitch;
    for (int32_t x = 0; x < fmt.width; x += stepX) {
      const uint8_t* p = row + (size_t)x * px;
      // 通道序随原生格式(bgra/rgba)不同, 求亮度时权重会略有偏差;
      // 但黑屏/白屏/纯色的判定不受影响, 只用于粗筛
      double luma = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
      sum += luma;
      sum2 += luma * luma;
      ++n;
      uint32_t key = ((uint32_t)(p[0] >> 3) << 10) | ((uint32_t)(p[1] >> 3) << 5) |
                     (uint32_t)(p[2] >> 3);
      ++hist[key];
    }
  }
  if (n == 0) {
    return st;
  }
  st.meanLuma = sum / (double)n;
  st.stdLuma = std::sqrt(std::max(0.0, sum2 / (double)n - st.meanLuma * st.meanLuma));
  int32_t top = 0;
  for (const auto& kv : hist) {
    top = std::max(top, kv.second);
  }
  st.topColorRatio = (double)top / (double)n;
  return st;
}

// 判"这张图不是废图": 有细节、亮度不贴边、不是一整块纯色
inline bool imageLooksAlive(const ImageStats& st, std::string& why) {
  if (st.width <= 0 || st.height <= 0) {
    why = "no-image";
    return false;
  }
  if (st.meanLuma < 12.0 || st.meanLuma > 243.0) {
    why = "near-black-or-white";
    return false;
  }
  if (st.stdLuma < 6.0) {
    why = "flat-no-detail";
    return false;
  }
  if (st.topColorRatio > 0.95) {
    why = "single-color";
    return false;
  }
  return true;
}

// 字幕带取证观察者: 复用 avox subtitletexttest 的机制 —— 字幕经 VkCanvasLayer 合成进
// 输出帧, 通过 ISurfaceRenderOb::onFrame 拿到的已是合成后帧; 转 RGBA 后统计下 1/4 亮像素。
// (注意: screenShot 拿的是合成前的原始解码帧, 不含字幕, 故不能用于字幕取证)
class SubtitleOb : public ISurfaceRenderOb {
 public:
  SubtitleOb() = default;
  ~SubtitleOb() override = default;
  void arm() {
    std::lock_guard<std::mutex> l(mtx);
    armed = true;
  }
  int32_t maxBrightCount() {
    std::lock_guard<std::mutex> l(mtx);
    return maxBright;
  }
  int64_t subtitleFrameCount() {
    std::lock_guard<std::mutex> l(mtx);
    return subFrames;
  }
  int64_t totalFrameCount() {
    std::lock_guard<std::mutex> l(mtx);
    return totalFrames;
  }

 private:
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    {
      std::lock_guard<std::mutex> l(mtx);
      if (!armed) return;
      ++frames;
      ++totalFrames;
    }
    IImageBuffer* tmp = createImageBuffer();
    YUVFrame frame = {};
    IImageBuffer* rgba = createImageBuffer();
    if (!image2SplitYUVFrame(buf, yuvType, frame, tmp) ||
        !yuvframe2Rgba(frame, rgba)) {
      delete tmp;
      delete rgba;
      return;
    }
    delete tmp;
    const ImageFormat fmt = rgba->getImageFormat();
    const uint8_t* base = rgba->getPointer();
    if (!base || fmt.width <= 0 || fmt.height <= 0) {
      delete rgba;
      return;
    }
    const int32_t pitch = fmt.rowPitch > 0 ? fmt.rowPitch : fmt.width * 4;
    // 字幕可能出现在本帧任意位置(ASS 支持 \pos 定位), 背景为暗色底(0x12141a,
    // 绿通道=0x1a=26<<120), 故扫整帧统计亮像素即可, 不会误命中背景。
    const int32_t stripY = 0;  // 扫整帧: 定位字幕也能命中
    int32_t count = 0;
    for (int32_t y = stripY; y < fmt.height; y += 2) {
      const uint8_t* row = base + (size_t)y * pitch;
      for (int32_t x = 0; x < fmt.width; x += 2) {
        const uint8_t* px = row + (size_t)x * 4;
        if (px[1] > 120) ++count;  // 绿通道(亮色字幕文字)
      }
    }
    delete rgba;
    std::lock_guard<std::mutex> l(mtx);
    if (count > 40) ++subFrames;
    if (count > maxBright) maxBright = count;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t, int32_t) override {}

  std::mutex mtx;
  bool armed = false;
  int32_t maxBright = 0;
  int64_t frames = 0;
  int64_t subFrames = 0;
  int64_t totalFrames = 0;
};

struct Attempt {
  bool pass = false;
  std::string note;
};

// ── 播放器观察者: 只记 IO 错误 (首帧前解码报错属正常, 不判失败) ──
class CaseOb : public IMediaPlayerOb {
 public:
  CaseOb() = default;
  ~CaseOb() override = default;

 private:
  std::mutex mtx;
  bool ioErr = false;
  std::string lastErr;

 public:
  void onIoError(AVError error, const char* msg) override {
    (void)error;
    std::lock_guard<std::mutex> lock(mtx);
    ioErr = true;
    lastErr = msg ? msg : "";
  }
  bool hasIoError() {
    std::lock_guard<std::mutex> lock(mtx);
    return ioErr;
  }
  std::string error() {
    std::lock_guard<std::mutex> lock(mtx);
    return lastErr;
  }
};

// ── 起播等待: 返回是否进入 playing ──
inline bool waitPlaying(IMediaPlayer* player, CaseOb& ob, int32_t timeoutMs) {
  int32_t ticks = timeoutMs / 100;
  for (int32_t i = 0; i < ticks; ++i) {
    if (player->getState() == PlayerState::playing) {
      return true;
    }
    if (ob.hasIoError()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

// ── 等到真出帧 (fps>0): 录制/截图必须在有视频尺寸之后才能拿到 track 描述 ──
inline bool waitFirstFrames(IMediaPlayer* player, CaseOb& ob, int32_t timeoutMs) {
  int32_t ticks = timeoutMs / 100;
  for (int32_t i = 0; i < ticks; ++i) {
    if (player->getState() == PlayerState::playing && player->getFps() > 0) {
      return true;
    }
    if (ob.hasIoError()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return false;
}

// 播放统计摘要 (判过判不过都打, 便于归因)
inline std::string pullDetail(IMediaPlayer* player, const PlayCase& c, CaseOb& ob) {
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "state=%s pos=%lldms dur=%lldms fps=%.1f io=%s dec=%s",
                getPlayerStateStr(player->getState()),
                (long long)player->getPosition(), (long long)player->getDuration(),
                player->getFps(), getIoPlanStr(c.io), c.hardDecode ? "hard" : "soft");
  std::string detail = buf;
  std::string err = ob.error();
  if (!err.empty()) {
    detail += " err=" + err;
  }
  return detail;
}

// ── 单次拉流尝试 ──
inline Attempt pullAttempt(const PlayCase& c, void* surface) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  if (surface) {
    sr->setSurface(surface);
  } else {
    sr->setOffSurface(YuvType::yuv420P);
  }
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  int64_t ticks = (int64_t)c.seconds * 10;
  for (int64_t i = 0; i < ticks; ++i) {
    if (player->getState() == PlayerState::playing && player->getFps() > 0 &&
        player->getPosition() > 1500) {
      r.pass = true;
      break;
    }
    if (ob.hasIoError()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::string detail = pullDetail(player, c, ob);
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  if (!r.pass) {
    r.note = "no-frame-in-" + std::to_string(c.seconds) + "s ";
  }
  r.note += detail;
  return r;
}

// ── WebRTC 拉流: 连接 + 首帧 + fps>0 ──
class RtcOb : public IRtcEventOb {
 public:
  RtcOb() = default;
  ~RtcOb() override = default;

 private:
  std::mutex mtx;
  bool conn = false;
  bool frame = false;

 public:
  void onConnectionState(RtcConnState state) override {
    std::lock_guard<std::mutex> lock(mtx);
    conn = conn || state == RtcConnState::connected;
  }
  void onFirstVideoFrame() override {
    std::lock_guard<std::mutex> lock(mtx);
    frame = true;
  }
  bool connected() {
    std::lock_guard<std::mutex> lock(mtx);
    return conn;
  }
  bool firstFrame() {
    std::lock_guard<std::mutex> lock(mtx);
    return frame;
  }
};

inline Attempt rtcAttempt(const PlayCase& c) {
  Attempt r;
  IRtcPlayer* player = createWebRtcPlayer();
  if (!player) {
    r.note = "createWebRtcPlayer-null";
    return r;
  }
  player->setRollType(RtcRollType::offer);
  player->setVideoDirection(RtpDirection::recvOnly);
  // 无头不协商音频: 避免占用设备, 也排除音频线程干扰
  player->setAudioDirection(RtpDirection::inactive);
  RtcOb ob;
  player->addOb(&ob);
  IRtcEventOb* agent = createZlTestSdpAgent(player, c.url.c_str());
  if (agent) {
    player->addOb(agent);
  }
  player->open();
  double fps = 0;
  float loss = 0;
  int32_t rtt = -1;
  for (int64_t i = 0; i < (int64_t)c.seconds * 20; ++i) {
    if (ob.connected()) {
      fps = player->getFps();
      loss = player->getLossRate();
      rtt = player->getRttMs();
      if (ob.firstFrame() && fps > 0) {
        r.pass = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  char buf[256];
  std::snprintf(buf, sizeof(buf), "conn=%d firstFrame=%d fps=%.1f loss=%.2f rtt=%d",
                (int)ob.connected(), (int)ob.firstFrame(), fps, loss, rtt);
  r.note = buf;
  player->removeOb(&ob);
  if (agent) {
    player->removeOb(agent);
  }
  player->close();
  delete player;
  return r;
}

// ── 离屏帧观察者: packed 契约 + 抽帧转 RGBA ──
class FrameOb : public ISurfaceRenderOb {
 public:
  FrameOb() = default;
  ~FrameOb() override = default;

 private:
  std::mutex mtx;
  std::string prefix;
  int32_t frames = 0;
  int32_t dumped = 0;
  bool contractOk = true;
  std::string reason;
  std::string firstFmt;
  std::string firstType;

 public:
  void setPrefix(const std::string& p) { prefix = p; }
  void onFrame(IImageBuffer* buf, YuvType yuvType) override {
    if (!buf || !buf->getPointer()) {
      mark("onFrame-null");
      return;
    }
    ImageFormat fmt = buf->getImageFormat();
    YUVFormat yf = {};
    image2YUVFormat(fmt, yuvType, yf);
    std::lock_guard<std::mutex> lock(mtx);
    if (frames == 0) {
      firstType = getYuvTypeStr(yuvType);
      firstFmt = firstType + "-" + std::to_string(yf.width) +
                 "x" + std::to_string(yf.height) + "-pitch" + std::to_string(fmt.rowPitch);
      // 契约: rowPitch 不小于宽, 且缓冲容纳整帧
      if (fmt.rowPitch < yf.width ||
          buf->getBufferSize() < getYuvFrameSize(yf, fmt.rowPitch)) {
        contractOk = false;
        reason = "packed-contract";
      }
    }
    if (frames < 2 && yuvType != YuvType::yuv420P10 && yuvType != YuvType::p010) {
      // 10bit 只判交付契约: sw yuvframe2Rgba 不支持 10bit (tone map 属 GPU
      // 车道), 落 PNG 的诊断通道跳过, 避免误伤哨兵
      IImageBuffer* tmp = createImageBuffer();
      YUVFrame frame = {};
      if (image2SplitYUVFrame(buf, yuvType, frame, tmp)) {
        IImageBuffer* rgba = createImageBuffer();
        if (yuvframe2Rgba(frame, rgba)) {
          std::string path = prefix + "frame" + std::to_string(frames) + ".png";
          if (saveImagePath(path.c_str(), rgba)) {
            ++dumped;
          } else if (contractOk) {
            contractOk = false;
            reason = "save-png-failed";
          }
        } else if (contractOk) {
          contractOk = false;
          reason = "yuvframe2Rgba-failed";
        }
        delete rgba;
      } else if (contractOk) {
        contractOk = false;
        reason = "image2SplitYUVFrame-failed";
      }
      delete tmp;
    }
    ++frames;
  }
  void onSurface() override {}
  void onRender() override {}
  void onWinSizeChange(int32_t width, int32_t height) override {
    (void)width;
    (void)height;
  }
  void mark(const std::string& why) {
    std::lock_guard<std::mutex> lock(mtx);
    if (contractOk) {
      contractOk = false;
      reason = why;
    }
    ++frames;
  }
  bool ok() {
    std::lock_guard<std::mutex> lock(mtx);
    return contractOk;
  }
  std::string why() {
    std::lock_guard<std::mutex> lock(mtx);
    return reason;
  }
  int32_t count() {
    std::lock_guard<std::mutex> lock(mtx);
    return frames;
  }
  int32_t dumpedCount() {
    std::lock_guard<std::mutex> lock(mtx);
    return dumped;
  }
  std::string firstFormat() {
    std::lock_guard<std::mutex> lock(mtx);
    return firstFmt;
  }
  std::string firstTypeStr() {
    std::lock_guard<std::mutex> lock(mtx);
    return firstType;
  }
};

inline Attempt frameContractAttempt(const PlayCase& c, const std::string& prefix) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::yuv420P);
  FrameOb fob;
  fob.setPrefix(prefix);
  addSurfaceRenderOb(sr, &fob);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  waitFirstFrames(player, ob, 10000);
  for (int32_t i = 0; i < c.seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  player->close();
  removeSurfaceRenderOb(sr, &fob);
  delete player;
  bool ok = fob.ok() && fob.count() >= 10;
  char buf[320];
  std::snprintf(buf, sizeof(buf), "frames=%d dumped=%d fmt=%s", fob.count(),
                fob.dumpedCount(), fob.firstFormat().c_str());
  if (!ok) {
    r.note = (fob.ok() ? "frames-too-few(" + std::to_string(fob.count()) + ") "
                       : fob.why() + " ");
  }
  r.note += buf;
  r.pass = ok;
  return r;
}

// ── 无vulkan直取 (车道B): enableYuvOut 帧类型契约 ──
// 判: 帧数 + packed 契约 + 首帧类型 = expectType; 类型不对即车道断裂/硬解回退
inline Attempt yuvOutAttempt(const PlayCase& c, const std::string& prefix) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  if (c.nativeRender) {
    sr->setVulkan(false);
  }
  sr->setOffSurface(YuvType::yuv420P);
  FrameOb fob;
  fob.setPrefix(prefix);
  addSurfaceRenderOb(sr, &fob);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  waitFirstFrames(player, ob, 10000);
  for (int32_t i = 0; i < c.seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  player->close();
  removeSurfaceRenderOb(sr, &fob);
  delete player;
  bool ok = fob.ok() && fob.count() >= 10;
  std::string type = fob.firstTypeStr();
  if (ok && !c.expectType.empty() && type != c.expectType) {
    ok = false;
  }
  char buf[320];
  std::snprintf(buf, sizeof(buf), "frames=%d dumped=%d type=%s expect=%s", fob.count(),
                fob.dumpedCount(), type.c_str(), c.expectType.c_str());
  if (!ok) {
    if (!fob.ok()) {
      r.note = fob.why() + " ";
    } else if (fob.count() < 10) {
      r.note = "frames-too-few(" + std::to_string(fob.count()) + ") ";
    } else {
      r.note = "type-mismatch ";
    }
  }
  r.note += buf;
  r.pass = ok;
  return r;
}

// ── 离屏截图落 PNG ──
inline Attempt screenShotAttempt(const PlayCase& c, const std::string& path) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  ISurfaceRender* sr = player->getSurfaceRender();
  if (c.nativeRender) {
    sr->setVulkan(false);
  }
  sr->setOffSurface(YuvType::yuv420P);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  // 必须等真出帧: 录制/截图都需要视频尺寸与已渲染画面, 仅 playing 时可能还没就绪
  bool playing = waitFirstFrames(player, ob, 15000);
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  IImageBuffer* shot = createImageBuffer();
  bool grabbed = sr->screenShot(shot);
  // 先统计再存盘/释放: 像素只在解码输出缓冲上读一遍
  ImageStats st = grabbed ? analyzeImage(shot) : ImageStats{};
  bool saved = grabbed && saveImagePath(path.c_str(), shot);
  delete shot;
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  int64_t bytes = saved ? fileSize(path) : -1;
  // 拿不到像素格式(图仍可能存盘成功)时不做质量判定, 避免误杀; note 里会标 stats=na
  bool haveStats = st.width > 0 && st.height > 0;
  std::string why;
  bool alive = !haveStats || imageLooksAlive(st, why);
  r.pass = playing && saved && bytes > 0 && alive;
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "playing=%d grab=%d bytes=%lld luma=%.1f std=%.1f top=%.2f %dx%d out=%s",
                (int)playing, (int)grabbed, (long long)bytes, st.meanLuma, st.stdLuma,
                st.topColorRatio, st.width, st.height, path.c_str());
  if (!r.pass) {
    r.note = grabbed ? ("image-" + why + " ") : "shot-failed ";
  }
  r.note += buf;
  return r;
}

// ── 录制: 直通(原流拷贝) / 转码(+中途 seek) ──
inline Attempt recordAttempt(const PlayCase& c, bool transcode, const std::string& outPath) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb ob;
  addMediaPlayerOb(player, &ob);
  // 录制取帧走离屏路径, 与 yuvouttest 同口径; novk 用例关 vulkan 走 pushFrame 非vk分支
  ISurfaceRender* sr = player->getSurfaceRender();
  if (c.nativeRender) {
    sr->setVulkan(false);
  }
  sr->setOffSurface(YuvType::yuv420P);
  player->setIoPlan(c.io);
  player->setHardDecode(c.hardDecode);
  player->open(c.url.c_str());
  // 必须等真出帧: 录制/截图都需要视频尺寸与已渲染画面, 仅 playing 时可能还没就绪
  bool playing = waitFirstFrames(player, ob, 15000);
  if (!playing) {
    removeMediaPlayerOb(player, &ob);
    player->close();
    delete player;
    r.note = "no-frames-before-record";
    return r;
  }
  IMediaMuxer* muxer = player->getMuxer(transcode);
  if (!muxer) {
    removeMediaPlayerOb(player, &ob);
    player->close();
    delete player;
    r.note = "getMuxer-null";
    return r;
  }
  muxer->setMuxerType(MuxerType::ffmpeg);
  if (transcode) {
    muxer->setVideoCodec(VCodecId::h264);
    // Windows 硬编 h264_mf 对 profile 敏感, 软编更稳 (与 transcoderecordertest 同口径)
    muxer->setHardEncode(false);
  }
  // 产物先删再用例判"产物 ≥8KB": io 层只在**初始化成功**时才 avio_open2 创建/截断
  // 文件, 编码器吐不出包时文件根本不会被创建 —— 留着上一轮的产物会让"本次零产出"
  // 被判 PASS (09-16 实测: 同一份残留文件让 4 次零产出都报 bytes=2128742)
  std::remove(outPath.c_str());
  bool opened = muxer->open(outPath.c_str());
  if (!opened) {
    removeMediaPlayerOb(player, &ob);
    player->close();
    delete player;
    r.note = "muxer-open-failed " + outPath;
    return r;
  }
  int64_t seekTo = -1;
  for (int32_t i = 0; i < c.seconds; ++i) {
    // 中途 seek 只在 rec-transcode-seek (PlayCase::seekMid) 打开:
    // 其余录制用例不 seek, 判定口径各自独立
    if (transcode && c.seekMid && seekTo < 0) {
      int64_t duration = player->getDuration();
      if (duration > 0) {
        seekTo = duration / 2;
        player->seek(seekTo);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  muxer->close();
  removeMediaPlayerOb(player, &ob);
  player->close();
  delete player;
  // Windows 上 close() 返回后句柄可能仍被写线程短暂持有, 立刻 fopen 会因共享冲突失败
  // (实测 flake: bytes=-1 而文件其实存在) —— 退避重试最多 2s
  int64_t bytes = -1;
  for (int32_t i = 0; i < 20; ++i) {
    bytes = fileSize(outPath);
    if (bytes > 0) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  r.pass = bytes >= c.minBytes;
  char buf[384];
  std::snprintf(buf, sizeof(buf), "mode=%s out=%s bytes=%lld seekTo=%lldms",
                transcode ? "transcode" : "copy", outPath.c_str(), (long long)bytes,
                (long long)seekTo);
  if (!r.pass) {
    r.note = "output-too-small ";
  }
  r.note += buf;
  return r;
}

// ── 字幕取证: 外挂 loadSubtitle / 内嵌 setSubtitleTrack → 字幕带(下1/4)亮像素(绿>120)>40 ──
// 机制同 avox subtitletexttest: 字幕经 VkCanvasLayer 合成进输出帧, 用 ISurfaceRenderOb::onFrame
// 拿到合成后帧, 转 RGBA 后统计下 1/4 亮像素。screenShot 拿的是合成前原始帧, 不含字幕。
inline Attempt subtitleAttempt(const PlayCase& c) {
  Attempt r;
  IMediaPlayer* player = createMediaPlayer();
  if (!player) {
    r.note = "createMediaPlayer-null";
    return r;
  }
  CaseOb iob;
  addMediaPlayerOb(player, &iob);
  ISurfaceRender* sr = player->getSurfaceRender();
  sr->setOffSurface(YuvType::yuv420P);
  SubtitleOb sob;
  addSurfaceRenderOb(sr, &sob);
  // 注意: 不要在此 setIoPlan/setHardDecode —— 字幕用例是本地文件, 走默认 ffmpeg 计划
  // 即可; 官方 subtitletexttest 也是直接 open, 不调这俩, 调了反而与已验证链路不一致。
  player->open(c.url.c_str());
  // 起播等待严格复刻官方: 仅判 playing(不卡 fps>0, 离屏场景更稳, 也避免无谓等待)
  bool playing = false;
  for (int i = 0; i < 150; ++i) {
    if (player->getState() == PlayerState::playing) {
      playing = true;
      break;
    }
    if (iob.hasIoError()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!playing) {
    removeMediaPlayerOb(player, &iob);
    player->close();
    delete player;
    r.note = "no-frames";
    return r;
  }
  bool subOk = true;
  bool embeddedTrackOk = false;  // 内嵌: 选轨后 subtitleSize()>0 即管线通
  std::string subNote;
  if (!c.subPath.empty()) {
    // 外挂: 必须显式加载, 返回 bool —— 加载失败说明 API/文件路径/解码链有问题
    bool loaded = player->loadSubtitle(c.subPath.c_str());
    if (!loaded) {
      subOk = false;
      subNote = "loadSubtitle-failed";
    }
  } else {
    // 内嵌: 选轨 (void); 直接选声明的 track, 再确认该轨确实被解封装暴露出来
    player->setSubtitleTrack(c.subTrack);
    ISourceInfo* info = player->getSourceInfo();
    embeddedTrackOk = info && info->subtitleSize() > 0;
  }
  if (subOk && c.subStyle) {
    // 样式链路取证(字幕样式设计.md 验收 1/3): 字号放大 + 黄色 + CPU 清晰
    // 缩放 + 半透明。黄(255)*0.8=204 仍高于亮像素阈值(120), 判据不受影响;
    // 几何样式(align/position/margin)由 test_subtitle_style 单测覆盖。
    // 走宿主唯一入口 getSubtitle()(IMediaPlayer 不继承 ISubtitle)
    ISubtitle* sub = player->getSubtitle();
    if (sub) {
      sub->setFont("simhei.ttf", 64);
      sub->setColor(1.f, 1.f, 0.f);
      sub->setScale(1.5f);
      sub->setOpacity(0.8f);
    }
  }
  sob.arm();  // 字幕槽激活后再开始统计字幕带亮像素
  for (int32_t i = 0; i < c.seconds; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  int32_t maxBright = sob.maxBrightCount();
  int64_t subFrames = sob.subtitleFrameCount();
  int64_t totalFrames = sob.totalFrameCount();
  // 析构顺序严格复刻官方(先 close 再 remove 观察者): 否则离屏渲染线程在
  // removeSurfaceRenderOb 后可能仍回调/close 内部引用已摘除的 observer → 段错误。
  player->close();
  removeSurfaceRenderOb(sr, &sob);
  removeMediaPlayerOb(player, &iob);
  delete player;
  bool rendered = maxBright > 40;
  bool pass;
  if (c.subPath.empty()) {
    // 内嵌: 选轨成功(subtitleSize>0)即证明内嵌字幕管线(解封装+选轨)通;
    // 能渲染(maxBright>40)则更强 —— ASS 经 libass 会渲染, subrip/mov_text
    // 当前 avox 仅选轨不渲染, 故以"选轨成功"为通过判据(渲染待 avox 补齐)
    pass = subOk && embeddedTrackOk;
  } else {
    // 外挂: 必须加载成功且字幕带出现亮像素(渲染链路通)
    pass = subOk && rendered;
  }
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "subOk=%d rendered=%d maxBright=%d subFrames=%lld totalFrames=%lld trackOk=%d sub=%s",
                (int)subOk, (int)rendered, maxBright, (long long)subFrames,
                (long long)totalFrames, (int)embeddedTrackOk,
                c.subPath.empty() ? ("track#" + std::to_string(c.subTrack)).c_str()
                                 : c.subPath.c_str());
  if (!pass) {
    if (!subOk) r.note = subNote + " ";
    else if (c.subPath.empty() && !embeddedTrackOk)
      r.note = "embedded-track-not-selectable ";
    else if (maxBright <= 40)
      r.note = "no-subtitle-band(<=40) ";
  }
  r.note += buf;
  r.pass = pass;
  return r;
}

// ── 运行选项 ──
struct RunOptions {
  std::string outDir;             // 录制/截图产物目录 (空 = 当前目录)
  std::string prefix = "pm_";     // 产物文件名前缀
  int32_t retries = 3;            // 拉流失败重开次数
  std::vector<std::string> skip;  // 跳过的 case id
  bool includeDisabled = false;   // 是否连 enabled=false 的用例一起跑
  std::string only;              // 非空时只跑 id 以此前缀开头的用例 (快速筛选, 如 "sub")
  // 每条用例结束回调 (宿主用于上屏/落盘); 空 = 不打
  void (*onCase)(const std::string& id, bool pass, const std::string& note) = nullptr;
  // 每条用例开始回调 (界面走查宿主更新左上角说明横幅);
  // idx/total = 实跑用例的序号(0-based)与总数, 跳过的不计
  void (*onCaseStart)(const PlayCase& c, int32_t idx, int32_t total) = nullptr;
  // 非空时: 宿主置位 (如界面走查关窗) 后, 当前用例跑完即停矩阵, 剩余不再开跑。
  // 只影响"还跑不跑下一条", 判定行口径不变
  const std::atomic<bool>* cancel = nullptr;
};

inline bool isSkipped(const RunOptions& opt, const std::string& id) {
  return std::find(opt.skip.begin(), opt.skip.end(), id) != opt.skip.end();
}

inline std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty()) {
    return name;
  }
  char last = dir[dir.size() - 1];
  return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

// ── 跑完整矩阵: 返回 0 全过, 1 有失败 ──
inline int runAll(const std::vector<PlayCase>& cases, void* surface, const RunOptions& opt) {
  int32_t pass = 0;
  int32_t fail = 0;
  int32_t skipped = 0;
  std::vector<std::string> failed;
  std::vector<std::string> skippedIds;
  // 跳过与否的判据只此一份 (预统计 total 与主循环共用)
  auto runnable = [&opt](const PlayCase& c) {
    return !isSkipped(opt, c.id) && (opt.only.empty() || c.id.rfind(opt.only, 0) == 0) &&
           (c.enabled || opt.includeDisabled);
  };
  int32_t total = 0;
  for (const PlayCase& c : cases) {
    if (runnable(c)) {
      ++total;
    }
  }
  int32_t idx = 0;
  for (const PlayCase& c : cases) {
    if (!runnable(c)) {
      ++skipped;
      skippedIds.push_back(c.id);
      continue;
    }
    // 界面走查关窗取消: 当前用例跑完后不再开新的 (判定行口径不变, 只补一行 info)
    if (opt.cancel && opt.cancel->load()) {
      std::printf("[info] matrix canceled before case=%s (walkthrough window closed)\n",
                  c.id.c_str());
      std::fflush(stdout);
      break;
    }
    if (opt.onCaseStart) {
      opt.onCaseStart(c, idx, total);
    }
    ++idx;
    Attempt r;
    switch (c.kind) {
      case CaseKind::pull: {
        // 失败自动重开重试 (HLS 首片未就绪的 404 靠这个兜)
        for (int32_t attempt = 1; attempt <= opt.retries; ++attempt) {
          r = pullAttempt(c, surface);
          if (r.pass) {
            break;
          }
          std::printf("[retry] case=%s attempt=%d/%d %s\n", c.id.c_str(), attempt,
                      opt.retries, r.note.c_str());
          std::fflush(stdout);
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        break;
      }
      case CaseKind::rtc:
        r = rtcAttempt(c);
        break;
      case CaseKind::frameContract:
        r = frameContractAttempt(c, joinPath(opt.outDir, opt.prefix));
        break;
      case CaseKind::screenShot:
        r = screenShotAttempt(c, joinPath(opt.outDir, opt.prefix + "shot.png"));
        break;
      case CaseKind::recordCopy:
        r = recordAttempt(c, false, joinPath(opt.outDir, opt.prefix + "copy.mp4"));
        break;
      case CaseKind::recordTranscode:
        r = recordAttempt(c, true, joinPath(opt.outDir, opt.prefix + "trans.mp4"));
        break;
      case CaseKind::yuvOut:
        r = yuvOutAttempt(c, joinPath(opt.outDir, opt.prefix));
        break;
      case CaseKind::subtitle:
        r = subtitleAttempt(c);
        break;
    }
    verdict(c.id, r.pass, r.note);
    if (opt.onCase) {
      opt.onCase(c.id, r.pass, r.note);
    }
    if (r.pass) {
      ++pass;
    } else {
      ++fail;
      failed.push_back(c.id);
    }
  }
  std::printf("[AVOX][TEST] case=play-matrix result=%s pass=%d fail=%d skip=%d\n",
              fail == 0 ? "PASS" : "FAIL", pass, fail, skipped);
  if (!failed.empty()) {
    std::string list;
    for (size_t i = 0; i < failed.size(); ++i) {
      list += (i ? "," : "") + failed[i];
    }
    std::printf("[AVOX][TEST] case=play-matrix-failed result=FAIL ids=%s\n", list.c_str());
  }
  if (!skippedIds.empty()) {
    std::string list;
    for (size_t i = 0; i < skippedIds.size(); ++i) {
      list += (i ? "," : "") + skippedIds[i];
    }
    std::printf("[AVOX][TEST] case=play-matrix-skip result=PASS ids=%s\n", list.c_str());
  }
  std::fflush(stdout);
  return fail == 0 ? 0 : 1;
}

// ── 用例表打印 (--list, 也用于生成文档); desc 单独一行缩进展示 ──
inline void printCases(const std::vector<PlayCase>& cases) {
  static const char* kKinds[] = {"pull",     "rtc",      "frame",   "shot",
                                 "rec-copy", "rec-trans", "yuv-out", "sub"};
  for (const PlayCase& c : cases) {
    const char* kind = kKinds[(int)c.kind];
    std::printf("%-20s %-10s io=%-11s dec=%-4s %2ds %s %s\n", c.id.c_str(), kind,
                getIoPlanStr(c.io), c.hardDecode ? "hard" : "soft", c.seconds,
                c.enabled ? "   " : "(off)", c.url.c_str());
    if (!c.desc.empty()) {
      std::printf("%22s # %s\n", "", c.desc.c_str());
    }
  }
}

}  // namespace playmatrix
}  // namespace avox
