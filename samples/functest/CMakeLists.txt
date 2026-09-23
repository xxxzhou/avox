

# 注: 以下纯逻辑探针已迁入 tests/ 的 doctest (见 tests/CMakeLists.txt), 此处不再注册:
#   mathtest -> test_math.cpp; sessiontest/teamtest -> avox_agent_tests;
#   jsontest/ringbuffertest/logtest/regedittest/cfunc/observertest/threadtest
#   -> test_json/test_ringbuffer/test_time/test_pipenode/test_observer/test_threadsync.cpp

# agent 端到端测试: 走 C 导出层发一次真实请求 (需要 agent.json 里的可用 key)。
# 不必编 core 源文件 —— 接口全是裸指针 + const char*, 无 std 容器跨 DLL 边界。
generatetest(agenttest)
if(MSVC)
    target_compile_options(agenttest PRIVATE /utf-8)
endif()
generatetest(recordertest)
generatetest(transcoderecordertest)
# 离线超分转码探针: 转码录制器 + Real-ESRGAN 逐帧出增强 mp4 (倍率/耗时表回写方案 P0-T1)
generatetest(enhancetest dbghelp)
if(MSVC)
    target_compile_options(enhancetest PRIVATE /utf-8)
endif()
generatetest(yuvouttest)
# HDR10 tone map 链路探针: PQ/BT.2020 素材经 Vulkan V5 渲染与 SDR 参考对比
generatetest(hdrtest)
# seek 出图回归: 起播→seek 指定位置→抽帧 PNG + 客观像素判据 (源码含中文字面量)
generatetest(seektest)
if(MSVC)
    target_compile_options(seektest PRIVATE /utf-8)
endif()
# 起播头几秒逐帧体检: 复现「打开文件开头花屏数秒后自愈」(源码含中文字面量)
generatetest(headtest)
if(MSVC)
    target_compile_options(headtest PRIVATE /utf-8)
endif()
# 变速不变调冒烟: avox_tempo 插件 DSP 比例 + 播放管线 2x 推进 (源码含中文字面量)
generatetest(tempotest)
if(MSVC)
    target_compile_options(tempotest PRIVATE /utf-8)
endif()
generatetest(latprobe)
# DX11VA 硬解中段分辨率变化停帧诊断探针 (全量日志 + 渲染事件逐秒统计)
generatetest(resizediag)
if(MSVC)
    target_compile_options(resizediag PRIVATE /utf-8)
endif()
generatetest(inputtest)
generatetest(httplibtest)
# 外挂 .srt 文本字幕端到端: loadSubtitle→TextRasterizer→canvas 合成→像素判据
if(AVOX_ENABLE_FREETYPE)
    generatetest(subtitletexttest)
    # SUBTEXT_PERF 基准直链 TextRasterizer(导出类), 头文件按此宏开门
    target_compile_definitions(subtitletexttest PRIVATE AVOX_ENABLE_FREETYPE)
    if(MSVC)
        target_compile_options(subtitletexttest PRIVATE /utf-8)
    endif()
endif()
if(AVOX_ENABLE_TRANSLATION)
    generatetest(translationtest)
endif()
# avox_ass 插件 M1 冒烟: assOverlayHub 加载/查表/骨架降级/缺插件降级
if(AVOX_ENABLE_ASS)
    generatetest(asstest)
    # ASS 内封字幕轨端到端: MKV 轨枚举/选轨/libass 合成/输出帧像素判据
    generatetest(assmkvtest)
    if(MSVC)
        target_compile_options(assmkvtest PRIVATE /utf-8)
    endif()
    # \pos 定位探针(插件级): 区分 libass 定位问题与播放管线问题
    generatetest(asspostest)
    if(MSVC)
        target_compile_options(asspostest PRIVATE /utf-8)
    endif()
    # ASS 字幕叠加视频演示: base.raw(assdemo_genbase.py 生成)→逐帧合成→comp.raw
    generatetest(assdemo)
    if(MSVC)
        # 源码含中文字面量(demo.ass 剧本), 需 UTF-8 执行字符集
        target_compile_options(assdemo PRIVATE /utf-8)
    endif()
endif()
if(AVOX_ENABLE_CV)
    generatetest(inpainttest)
endif()
# TTS 语音合成测试: 仅链核心 avox (sherpa kokoro 后端运行时从 plugins/ 加载)
generatetest(ttstest)
if(MSVC)
    # 源码含中文测试句字面量, 需以 UTF-8 解释源码/执行字符集
    target_compile_options(ttstest PRIVATE /utf-8)
endif()
# ARKit52 blendshape 推理测试 (wav2arkit_cpu ONNX go/no-go): 仅链核心 avox
# (OnnxModelUser/IONNXSession 在核心 vision/; avox_onnx 运行时从 plugins/ 加载)
generatetest(facetest)
if(MSVC)
    # 源码含中文注释字面量, 需以 UTF-8 解释源码/执行字符集
    target_compile_options(facetest PRIVATE /utf-8)
endif()
if(AVOX_ENABLE_OPENCV)
    generatetest(templatematchtest)
    generatetest(ocrttest ${OpenCV_LIBRARIES})
    target_include_directories(ocrttest PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
# 虚拟制片相机标定端到端测试 (avox_calib 插件运行期从 plugins/ 加载; 合成数据自测)
if(AVOX_ENABLE_CALIB)
    generatetest(calibtest)
    generatetest(calibrectest)
    if(MSVC)
        target_compile_options(calibtest PRIVATE /utf-8)
        target_compile_options(calibrectest PRIVATE /utf-8)
    endif()
endif()

if(AVOX_ENABLE_CLI)
    # 黑盒测试: 调用 avox_cli.exe 传各种参数并显示输出。
    # add_dependencies 保证 avox_cli 先构建, 运行时靠 exe 同级查找。
    generatetest(tooltest)
    add_dependencies(tooltest avox_cli)
    if(MSVC)
        # 源码含中文媒体路径字面量, 需以 UTF-8 解释源码/执行字符集
        target_compile_options(tooltest PRIVATE /utf-8)
    endif()
endif()

if(ONLY_LINUX)
    generatetest(waylandtest)
endif()

if(MSVC)
    # inputtest 源码含中文字面量, 需以 UTF-8 解释源码/执行字符集
    target_compile_options(inputtest PRIVATE /utf-8)
endif()

# 磁力/torrent 播放链路分步计时基准 (探测/起播/稳态/seek), 配 script/torrent_bench.py
if(AVOX_ENABLE_TORRENT)
    generatetest(torrentbench)
    if(MSVC)
        target_compile_options(torrentbench PRIVATE /utf-8)
    endif()
endif()

# SMB 远程源链路冒烟 (open/list/resolve/播放/seek), 需 avox_remote 带 libsmb2
# (库仓 windows/libsmb2 产物存在即定义 AVOX_REMOTE_SMB, 与插件同条件;
#  本目录作用域重新 find 一次, FindLibsmb2 的结果变量不跨目录传播)
find_package(Libsmb2 QUIET)
if(LIBSMB2_FOUND AND FFMPEG_LIBRARIES)
    generatetest(smbsourcetest)
    if(MSVC)
        # 源码含中文提示字面量, 需以 UTF-8 解释源码/执行字符集
        target_compile_options(smbsourcetest PRIVATE /utf-8)
    endif()
    # SMB 共享媒体播放普查(全量遍历逐个 open/推进/seek)
    generatetest(smbbatchtest)
    target_compile_options(smbbatchtest PRIVATE /utf-8)
endif()
