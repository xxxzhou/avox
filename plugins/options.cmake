# plugins/options.cmake —— 组件(plugins/)option 定义
#
# 根 CMakeLists.txt 在 include(AVOXOptions) 和 add_subdirectory(src) 之前 include 本文件,
# 保证 src 的 gate(如 inpaint 依赖 AVOX_ENABLE_OPENCV)能读到这些 option。
#
# 注: 第三方库的 find_package / link 过渡期仍在 cmake/AVOXOptions.cmake
#     (对应 src 模块还在用, 如 inpaint 用 opencv), 待各模块迁到 plugins 后再移入。
#     已迁组件的 option 先挪到这里, 未迁的随迁移逐个补。

option(AVOX_ENABLE_OPENCV "build with OpenCV support (for mask processing)" ON)
option(AVOX_ENABLE_SHERPA "build sherpa-onnx for streaming speech recognition" ON)
option(AVOX_ENABLE_ONNX "build ONNX Runtime support" ON)
option(AVOX_ENABLE_TRANSLATION "build offline translation" ON)
option(AVOX_ENABLE_CV "build avox_cv (inpaint watermark removal + generic YOLO, needs AVOX_ENABLE_ONNX+AVOX_ENABLE_OPENCV)" ON)
option(AVOX_ENABLE_OCR "build OCR text recognition (PP-OCRv6, needs AVOX_ENABLE_ONNX)" ON)
# avox_avatar: 音频->ARKit52 blendshape (虚拟人口型/表情; 运行期需 avox_onnx 插件 + 模型, 默认开)
option(AVOX_ENABLE_AVATAR "build avox_avatar (audio->ARKit52 blendshape, virtual human; needs AVOX_ENABLE_ONNX at runtime)" ON)
# OpenVINO: VkQEnhanceLayer 画质增强的 Intel iGPU/CPU 推理后端 (runtime 自动 GPU→CPU→ORT 降级)。
# 默认 OFF: 无 OV runtime 时 VkQEnhanceLayer 走 ORT CPU。提取 runtime: python script/openvino/extract_openvino.py
option(AVOX_ENABLE_OPENVINO "build OpenVINO for Intel iGPU/CPU inference (VkQEnhanceLayer)" OFF)
# avox_torrent: 磁力/BT 边下边播 (libtorrent 顺序下载, 插件静态链入)
option(AVOX_ENABLE_TORRENT "build avox_torrent magnet/bt streaming plugin (libtorrent)" ON)
# avox_decklink: DeckLink(Blackmagic)采集卡 (仅Windows编译, 运行期需Desktop Video驱动)
option(AVOX_ENABLE_DECKLINK "build avox_decklink DeckLink capture card plugin (Windows only, needs Desktop Video driver at runtime)" ON)
# avox_calib: 虚拟制片相机标定 (内参/手眼+scale/PnP/序列标定, 依赖 OpenCV calib3d+aruco; 移植自 aoce)
option(AVOX_ENABLE_CALIB "build avox_calib virtual production camera calibration (needs OpenCV)" ON)
# avox_fbx: FBX 场景解析 (ufbx 单文件解析器, 无外部依赖; 虚拟制片幕墙 mesh 标定用)
option(AVOX_ENABLE_FBX "build avox_fbx FBX scene import plugin (ufbx, no external deps)" ON)
# g2o 图优化: 虚拟制片标定 M2 (弧形幕墙内参 BA/手眼再优化; 需 3rdparty/g2o,eigen submodule
# 且 build_windows.py 构建过 g2o, 否则 avox_calib 自动降级 M1 OpenCV 路径)
option(AVOX_ENABLE_G2O "build avox_calib g2o graph optimizers (needs 3rdparty/g2o+eigen built)" ON)
# avox_ass: ASS/SSA(后续含 PGS)字幕 overlay 渲染 (libass 在独立仓 avox-ass-deps 预编译;
# 未指 AVOX_ASS_DEPS_DIR 时只编骨架, AssOverlay::init() 返 false 运行期降级)
option(AVOX_ENABLE_ASS "build avox_ass ASS/PGS subtitle overlay plugin (real rendering needs avox-ass-deps prebuilt libs)" ON)
# avox-ass-deps 产物目录(布局: include/ + lib/ + bin/); 空 = 骨架模式
set(AVOX_ASS_DEPS_DIR "" CACHE PATH "avox-ass-deps dist dir (libass prebuilt); empty = skeleton only")
# avox_tempo: 音频变速不变调 (SoundTouch WSOLA, 插件静态链入; 预编译依赖库仓
# library/<platform>/soundtouch, 构建脚本 script/soundtouch/build_windows.py; 未就位自动跳过)
option(AVOX_ENABLE_TEMPO "build avox_tempo speed-without-pitch plugin (SoundTouch)" ON)
