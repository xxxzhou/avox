# Linux 硬解硬编施工卡（夜班 2026-10-05）

> 状态: 进行中 · 上次核对: 2026-10-05 04:00 · 权威源: -


> 方案: [linux硬解硬编与VK管线对齐方案](linux硬解硬编与VK管线对齐方案.md)（v2，三路评审已并入）。
> 夜班窗口 2026-10-05 03:40 → 08:30，看门狗每 30 分钟一轮；**08:30 及以后的轮次只做终态收尾
> （汇总+遗留清单），不再新增改动**。每轮开工先读本文「状态一览」，收工必更新本文并 commit。

## 状态一览（每轮更新）

- [x] **P0-2** FFDecoder 收帧失败上浮 openFailed —— 完成，Windows 构建+ctest 2/2 绿
- [x] **P0-1** FFVkDecoder 注册 vp9/av1（`__ONLY_LINUX__` 门）+ hasVulkanHwaccel 探测 +
      AVTrack 宏与选型分支 —— 完成，同上验证（Windows 侧 FFVkDecoder 逐字节同源编译过）
- [x] **P1-a①** FFmpeg 白名单脚本改齐（encoder h264/hevc_vaapi + hwaccel vp9/av1_vaapi +
      --enable-libdrm）—— 完成；libdrm-dev 已装入 WSL（wsl -u root apt-get，04:00）
- [ ] **P1-a②** WSL FFmpeg 重编 —— <待启动/进行中/完成，产物回填情况写这里>
- [ ] **P1-b** FFVaapiEncoder + 四处接线 —— 未开始（任务卡见下）
- [ ] **P1-c** Linux 侧编译/ctest 验证 —— 未开始（依赖 P1-a② 产物回填 + P1-b）
- [ ] **真机验收** rec-transcode 出片/参数对照 —— 阻塞于无真 Linux 机，标注待真机即可
- [ ] **Windows 回归** play_regress --offline（P0-2 动了共享解码路径，建议本轮起跑一次）

## 环境事实（已核实，勿重查）

- Windows 侧: 仓 D:\Work\github\avox（main），构建树 build/windows/avox（VS17 生成器）。
  验证: `cmake --build build/windows/avox --config Release --target avox` +
  `ctest --test-dir build/windows/avox -C Release`（2 用例，~3s）。
- WSL (Ubuntu 24.04): avox 克隆在 **~/github/avox**（origin=github SSH）。同步配方（免网络）:
  `git -C ~/github/avox remote add win /mnt/d/Work/github/avox 2>/dev/null;
   git -C ~/github/avox status --porcelain | head -3   # 必须先确认干净
   git -C ~/github/avox fetch win main && git -C ~/github/avox reset --hard FETCH_HEAD`
  （9p 慢，fetch 小增量可接受；**WSL 树有未提交改动时禁止 reset --hard**）
- FFmpeg 源码树: **~/ffmpeg-src**（9.0.1）；构建命令:
  `wsl -e bash -lc "~/github/avox/script/ffmpeg/build_ffmpeg_linux.sh ~/ffmpeg-src ~/ffmpeg-out-linux"`
  输出在 ~/ffmpeg-out-linux/{bin,lib,include}。历史输出目录 ~/ffmpeg-out-linux 已存在（上轮产物同位覆盖）。
- WSL 已装: libdrm-dev（1005 root 装）、libva-dev、libvulkan-dev、libssl-dev。
- 产物回填: 对照 3rdparty/library/linux/ffmpeg 现有布局（libavcodec.so.63.1.101 等）
  从 ~/ffmpeg-out-linux/lib `cp -L` 回填，**保持文件名/soname 不变**；
  自检: `grep -a h264_vaapi libavcodec.so*` 有命中 + `grep -ldrm libavutil.pc`。
- 限流应对: 模型限流（错误1302/429）时 sleep 90s 再试；**不开并行子代理**，单线程推进。

## 每轮工作循环

1. `date` 判断: 已过 08:30 → 跳到第 5 步收尾。
2. 读本文状态一览，取第一个未完成项。
3. 按任务卡实施**一小块**（不跨任务留半成品）；代码改动后必须过对应构建
   （Windows 改动: cmake+ctest；Linux-only 改动: WSL 增量构建，长任务用 run_in_background
   落盘日志，本轮只启动/检查不干等）。
4. 更新本文（勾选、写产物状态、踩坑记入变更日志）→ `git add` 相关文件 → commit（首行≤50字中文）
   → `git push`（失败先 `git pull --rebase`）。
5. 08:30 后: 在本文追加「终态」一节（本夜成果/遗留/真机验收清单），不 commit 新代码。

## 任务卡

### P1-b FFVaapiEncoder（Linux-only，`#if defined(__ONLY_LINUX__) && defined(AVOX_ENABLE_FFMPEG)` 门）

新文件 `src/avox_ffmpeg/encoder/FFVaapiEncoder.{hpp,cpp}`，结构照 FFDx11Encoder（: public FFVEncoder）:

1. **挂进构建**: 先查 `src/avox_ffmpeg/CMakeLists.txt` 源文件列表方式（GLOB 则无需改，显式则追加）。
2. `onAttachContext` override（照 FFVADecoder.cpp:59-87 的 hwdevice 写法 + FFDx11Encoder.cpp:41-83
   的 hwframes 结构换 VAAPI）: `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VAAPI)`（device=NULL 按默认
   render node，P3 才 pin 节点）→ `av_hwframe_ctx_create_pool` 用 hwframes（sw_format=NV12、
   initial_pool_size=8、width/height 取视频 desc）→ `codecCtx->pix_fmt = AV_PIX_FMT_VAAPI` +
   `hw_device_ctx`/`hw_frames_ctx` 挂上。**别抄 FFDx11Encoder 的 `av_dict_set("surfaces",8)`
   （QSV 词汇，VAAPI 无效）**。
3. `encode(const YUVFrame&)` override（**第四处接线，最易漏**）: 基类 FFVEncoder::encode 直接把
   CPU 指针塞 data[]（FFVEncoder.cpp:180-192），对 h264_vaapi 必死——必须自己: 取视频 desc 建好
   hwframes 后 `av_hwframe_get_buffer` 得 hw 帧 → `av_hwframe_transfer_data(hw←cpu)` 上载 → 带
   pts/元数据 `avcodec_send_frame`。软编兜底路径（无 hwBuffer 时）直接调基类。
4. 注册: `AvoxManager.cpp` 顶部 extern 声明 + Linux 分支（:192-194）加 `regFFVaapiEncoder()`；
   注册名用现成宏 `AVOX_FFVAAPI_H264_ENCODER/H265`（Muxer.hpp:26-27，值 "ff_h264_vaapi"/"ff_hevc_vaapi"，
   自定义名与选型名严格一致即可，勿用 FFmpeg 真名 "h264_vaapi"——那个名字会被 regFFCodec 自动注册成
   **无 hw_device_ctx 的通用 FFVEncoder**，见方案 §2.2 陷阱A）。
5. 选型: `Muxer.cpp:36-64` getDefaultEncoderName 加 Linux 分支（h265 同款）:
   `#elif defined(__ONLY_LINUX__)  return bHard ? AVOX_FFVAAPI_H264_ENCODER : AVOX_FF_H264_ENCODER;`
   等等；**非 Android/Apple 原分支（D3D11 名）保留给 Windows 不动**。
6. preset 分发: `FFVEncoder.cpp:117-135` 加 vaapi 分支（按真名 strstr "_vaapi"）: quality→
   `global_quality`+`rc_mode=ICQ`，有 bitrate 时默认 VBR 不强设。基类 onPreEncoder 的
   bitrate/GOP/B帧=0/time_base/色彩标签（:93-111）对 hw 编码器无害，保留。
7. 已知遗留（写代码时规避）: agpl 渠道 Linux 无兜底名（D4 拍板前保持现状）；
   `Muxer.cpp:63` 对非 h264/h265 返 h264 软编名（vp9/av1 转码选型错，本夜不修，记录即可）。

### P1-c Linux 编译与验证

1. 产物回填后: WSL `cd ~/github/avox && python3 build_linux.py`（增量；构建树在
   ~/github/avox/build/linux/avox）。
2. `ctest --test-dir ~/github/avox/build/linux/avox --output-on-failure`。
3. 冒烟: WSL 无 /dev/dri → FFVADecoder/FFVaapiEncoder 应自动降级/明确报错，软解播放不回归
   （`LD_LIBRARY_PATH=. ./linuxtest <文件>` 起播即过）。
4. 矩阵解锁**不做**（方案 P1-2 勘误: CI release.yml 的 Linux runner 无 /dev/dri，rec-transcode
   摘出 LINUX_OFFLINE_SKIP 必红——保持跳过，真机验收时才本地解锁）。

## 变更日志（每轮追加）

- **2026-10-05 03:34-04:10 第一轮（主会话）**: P0 两项落地并验证（FFDecoder recvFailStreak 上浮;
  FFVk vp9/av1 Linux 车道+hwaccel 探测; AVTrack 宏/选型），Windows 构建 RC=0 + ctest 2/2 绿;
  FFmpeg 白名单脚本补齐（P1-a①）; libdrm-dev 装入 WSL; 施工卡建立。P1-a② FFmpeg 重编由本轮启动后台跑。
