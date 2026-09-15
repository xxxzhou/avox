# A-14 ~ A-19 增长期与储备项合篇

优先级 P2 · 里程碑 M5+/增长期 · 计划状态:摸底完毕(多数不启动,留底子与触发条件)

## A-14 avox_ohos 鸿蒙后端(M5)

- 现状:零代码,但**方案文档已在**:`doc/plan/鸿蒙.md`——XComponent 的 OH_NativeWindow →
  VkSurfaceKHR,直接复用现有 Vulkan 渲染管线(VkWindow/VkVideoRender/Layer);
  EGL 平台无关部分在 `src/avox_egl/` 但 ANativeWindow 绑定散布各处,复用需剥离。
- 启动时任务:DevEco 工程 + ohos CMake toolchain/build_ohos.py;XComponent 适配层;
  OHAudio 后端(对照 `src/avox_linux/PulseAudioRender.cpp` 模式);FFmpeg OHOS 构建。
- 风险(文档自列头号):XComponent OnSurfaceCreated 拿 OH_NativeWindow 的稳定性;
  EGL vs Vulkan 路线未拍板(文档倾向 Vulkan,建议直接定 Vulkan 免双路线)。

## A-15 Linux VAAPI

- 现状:**FFVADecoder 已实现**(`src/avox_ffmpeg/decoder/FFVADecoder.cpp:41` hwdevice 探测、
  :53 无设备降级软解、:102 VAAPI surface→CPU NV12),编译通过、真机未验;
  渲染后端已是 Vulkan(X11/Wayland)+ PulseAudio。
- 启动时任务:真机(非 WSL)验证;VAAPI→Vulkan 零拷贝导入迭代
  (FFVADecoder.hpp:12 注释预留);修文档漂移——code-wiki 仍写「VAAPI(计划)/ALSA」,
  与 platforms/linux 现状表矛盾,以代码为准回写。

## A-16 端侧视觉 API 化

- 现状:能力在、形态是「拉模式」——`IYoloDetector` detect/classify
  (`src/avox/AvoxVision.h:394`,工厂 :496);推模式先例:avatar 的
  `IVideoFace::feed + IVideoFaceOb` 回调(`src/avox/AvoxAvatar.h:78-97`);
  帧通路范本:`samples/vulkantest/facelandmarktest.cpp`(enableImage 回读 → feed)。
- 启动时任务:播放器→检测器订阅式封装(把 enableImage→feed 循环内置化,宿主只注册
  observer)+ 人位/机位结构化输出;monetization §8.1「YOLO 端侧版」吃这条。
- 风险:avox_cv 里有两份 YoloDetector(detect/ 疑水印专用、yolo/ 通用),API 化前先澄清入口;
  enableImage 回读是 RGBA 回程货(色彩矩阵过两次),原生 NV12 直取车道是性能前提。

## A-17 avox_remote 写操作

- 现状:零起点——全插件只有 PROPFIND 一个发送点(`DavSource.cpp:392-428`),
  无 GET/PUT/MKCOL/DELETE/MOVE;SMB 侧同样只读。
- 启动时任务:DavSource 方法族 + caps 能力位 + 错误码;SMB 对应封装;
  产品文件管理 UI(P2)吃接口。挂 a05 的 T1 契约设计上,别单独立项。

## A-18 磁力链接播放

- 现状:**技术完备,无代码任务**——libtorrent 引擎完整
  (`plugins/avox_torrent/TorrentEngine.hpp`:BEP-9 与 itorrents 竞速取元数据 :119-127、
  播放位置驱动顺序窗口+尾部预取 :147-153、readAt 阻塞保证 :154-157、
  进程级共享 session + LRU 磁盘缓存 :53-71);边下边播链路完整(IOParseTorrent 与
  IOParseSmb 同构),seek 看门狗放宽已有(MediaPlayer.cpp:1357-1360)。
- 纪律:默认不宣传、上架前法务评估(backlog 原口径),唯一待办是合规结论,不是代码。

## A-19 WebRTC/直播

- 现状:维持现状(ADR-0006)——`plugins/avox_webrtc` 能力面完整:IRtcPlayer 支持
  recvOnly/sendOnly/sendRecv、码率/编解码偏好、自动重连、fps/丢包/RTT 统计、
  ISignalChannel 信令;接口在核心层(`AvoxPlayer.h:297-332`)、实现在插件,
  边界与数据流见 `plugins/avox_webrtc/README.md`。
- 无计划任务;勾选项消费即可。
