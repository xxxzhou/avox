

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
generatetest(yuvouttest)
generatetest(latprobe)
generatetest(inputtest)
generatetest(httplibtest)
if(AVOX_ENABLE_TRANSLATION)
    generatetest(translationtest)
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
