

generatetest(jsontest)
generatetest(observertest)
# 会话事件日志的不变式自测 (surface 投影 / replace 校验 / resume 保真 / 编解码往返)
#
# 直接编译 core 的源文件参与, 不从 avox.dll 导入: avox 用静态 CRT (/MT), 而 core 的接口
# 大量使用 std::string / vector —— 跨 DLL 边界传它们会让对象在一个堆上构造、在另一个堆上
# 析构。core 是 DLL 内部设施, 跨边界一律走 avox_agent/export 的 C 接口。
generatetest(sessiontest)
target_sources(sessiontest PRIVATE
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/SessionTypes.cpp
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/Surface.cpp
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/Session.cpp
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/SessionCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/SessionPersistence.cpp
    # dsh 互通自测: 目录布局编码 + 附件仓 (publish 走核心图像解码, 链 avox 导出层)。
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/DshLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/avox_agent/adapter/DshAttachmentStore.cpp
    # BlockAssembler 组装测试 + 摘要核对: 这两个实现都未从 avox.dll 导出 (DLL 内部设施),
    # 与 core 源码一样直接编进测试。
    ${CMAKE_SOURCE_DIR}/src/avox_agent/core/Llm.cpp
    # ChatProvider wire 形状测试 (dsh #2724 tool 图片卸载): 同为 DLL 内部设施。
    ${CMAKE_SOURCE_DIR}/src/avox_agent/provider/ChatProvider.cpp
    # 模型路由重试臂测试 (dsh 0ca0f3d0b8 5 次重试默认): 策略同样编进测试。
    ${CMAKE_SOURCE_DIR}/src/avox_agent/policy/ModelRoutePolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/avox/module/Sha256.cpp
    # SessionCodec 的编码卫生 (patchInvalidUtf8) 实现在这, 同样未从 avox.dll 导出
    ${CMAKE_SOURCE_DIR}/src/avox/module/Utf8.cpp
)
if(MSVC)
    # 源码含中文断言消息与测试数据, 需以 UTF-8 解释源码/执行字符集
    target_compile_options(sessiontest PRIVATE /utf-8)
endif()

# agent 端到端测试: 走 C 导出层发一次真实请求 (需要 agent.json 里的可用 key)。
# 不必编 core 源文件 —— 接口全是裸指针 + const char*, 无 std 容器跨 DLL 边界。
generatetest(agenttest)
if(MSVC)
    target_compile_options(agenttest PRIVATE /utf-8)
endif()
generatetest(logtest)
generatetest(threadtest)
generatetest(ringbuffertest)
generatetest(regedittest)
generatetest(recordertest)
generatetest(transcoderecordertest)
generatetest(yuvouttest)
generatetest(latprobe)
generatetest(cfunc)
generatetest(inputtest)
generatetest(mathtest)
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
